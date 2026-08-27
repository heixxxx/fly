"""Dynamic 多右端项连续求解（EmIR dynamic IR drop 场景）。

同一矩阵 G 连续求解 T 个时间步的 G·x_t = b_t（b_t = f(x_{t-1})，严格串行）：
- master 只做非阻塞 kickoff（写 b_0 + 提交 kickoff task 后立即返回），
  编排链由 task 自驱动：check_dyn(t) 收敛时提交 controller(t)，controller(t)
  决定并提交 step t+1 的 task 组。master 上永远不做阻塞式流程。
- 单时间步求解 = nsd 个 compute 长 task + 1 个 check 长 task（v2 daemon 思路：
  PeerChannelGroup RPC 直连迭代），时间步之间 task 粒度隔离。
- 失败重跑原生支持（task 划分边界的核心目的）：每步结果 sol_t 持久化；
  组内失败原子传染（check 的全部副作用先于 respond done），restart 重投
  完整组后链自动恢复（check 重新提交 controller）。
- 冷启动安全：db 是权威数据源（temp 落盘恢复 + 持久对象），worker 进程
  缓存（LDLT 因子、粗校正 LU、cfg/coord 反序列化结果）纯加速——restart
  全新 run 下从 db 重建。
- warm start 默认启用：step t 的迭代初值取 sol_{t-1}（持久对象，restart
  后仍可用），相邻时间步解接近时迭代次数大幅下降。

会话标识 gen：每次 solve_ras_graph_dynamic 调用生成，随 task 参数链式
传递并作为全部进程缓存键前缀——同一 worker 池先后服务两次 solve（不同
矩阵）时缓存不串（restart 重投用 bin 里的原参数，gen 不变，缓存语义连续）。

对象命名空间（write_context_hash = f(name, module, args, inputs)，跨步同名
不同参数重写会被 provenance 拒绝，故跨步对象带时间步维度）：
    __rasg__b_{t}            temp   controller(t-1) 写，controller(t) 删除
    __rasg__sub_{sd} / coord / cfg / coarse_prebuilt   temp   kickoff 写，全程存活
    __fly_chan_{group_id}    持久   check listen 写，下一步 controller 删除
    __rasg__sol_{t}          持久   check 写（用户数据）
    __rasg__iters_{t} / converged_{t}   temp   check 写（controller 依赖锚点）
    __rasg__dynamic_done     temp   终止 controller 写（用户等待点）
"""
import os
import time
import pickle
import uuid
import numpy as np

from _fly_log import DBG, INFO, WARN, ERR
from fly import as_task, wait_obj
from agent import PeerChannelGroup, PeerRpcStatus, serialize_array, deserialize_array

# task 组优先级：高于默认 10——与集群其他任务共存时优先获得 idle worker
# （非抢占，仅影响就绪队列排序）。
_DYNAMIC_TASK_PRIORITY = 90

# compute→check 单次 RPC 超时（秒）。check task 失败后其业务端口残留
# （PeerRpcServer 是 agent 级），无限等待会让 compute 永久 RUNNING、
# master stop() drain 死等——有限超时后 raise 使组失败对 bin 可见。
# 收敛路径 check 侧有持久写 + controller 提交（秒级），60s 留足余量。
_RPC_TIMEOUT_SECONDS = 60.0


def solve_ras_graph_dynamic(db, matrix_ref, nsd, b0, update_rhs, num_steps,
                            overlap_ratio=0.50, max_iter=100, tol=1e-8,
                            omega=1.0, min_steps=2, sol_prefix="__rasg__sol",
                            max_concurrent_compute=None):
    """Dynamic 多右端项 kickoff（非阻塞，立即返回）。

    Args:
        db: Database（矩阵对象应已写入；同 db 重复调用本 API 需换 sol_prefix
            或清理旧对象——sol_t 同名同参数重写会被 provenance 跳过）。
        matrix_ref: 矩阵对象名（推荐）或 .npz 路径（本地实验）。
        nsd: 子域数。
        b0: 初始右端项（np.ndarray）。
        update_rhs: Callable[[np.ndarray, int], np.ndarray | None]——在
            controller task 内（worker 上）执行：输入上一步全局解 x_{t-1}
            与下一步号 t，返回 b_t；返回 None 提前终止。
        num_steps: 时间步总数（含 t=0）。
        min_steps: 每步最少迭代数（防冷启动早期残差假小误判；warm start
            相邻解接近，默认 2 比 v2 的 5 更激进）。
        sol_prefix: 结果对象名前缀，sol_t 写为 f"{sol_prefix}_{t}"。

    Returns:
        dict(sol_prefix, num_steps, db_path, gen)。用 get_dynamic_result(db)
        等待整体完成并取汇总；单步结果随时 db.read_object(f"{sol_prefix}_{t}")。
    """
    from fly.runtime import get_agent

    if num_steps < 1:
        raise ValueError(f"num_steps must be >= 1, got {num_steps}")

    n_workers = min(nsd, max_concurrent_compute) if max_concurrent_compute else nsd
    master = get_agent()
    # nsd 个 compute worker（attributes 轮转绑定 sd_i，钉住进程缓存）+
    # 1 个 check worker（A_fine/coarse LU 进程缓存跨步复用）。
    if not master.is_running() or master.worker_count < n_workers + 1:
        worker_configs = []
        for w in range(n_workers):
            assigned = [f"sd_{s}" for s in range(nsd) if s % n_workers == w]
            worker_configs.append({"attributes": assigned})
        worker_configs.append({"attributes": ["ras_check"]})
        master.launch_local_workers(worker_configs)
        assert master.wait_for_workers(n_workers + 1), \
            f"{n_workers + 1} workers should connect"

    gen = uuid.uuid4().hex[:8]
    INFO(f"[RASG DYN] kickoff: gen={gen} nsd={nsd} n_workers={n_workers + 1} "
         f"steps={num_steps} omega={omega} min_steps={min_steps}")

    # 清掉旧 run 的终止标记（防 get_dynamic_result 误读；其余旧对象语义见
    # 模块 docstring——同 db 重复调用建议换 sol_prefix）。
    try:
        db.remove_object("__rasg__dynamic_done")
    except Exception:
        pass

    db.write_object("__rasg__b_0", b0, save_to_db=False)

    kickoff_dyn_task(db, matrix_ref, nsd, overlap_ratio, max_iter, tol, omega,
                     min_steps, sol_prefix, num_steps, update_rhs, gen)
    return {"sol_prefix": sol_prefix, "num_steps": num_steps,
            "db_path": db.get_db_path(), "gen": gen}


def _kickoff_deps(db, matrix_ref, nsd, overlap_ratio, max_iter, tol, omega,
                  min_steps, sol_prefix, num_steps, update_rhs, gen):
    deps = [db.get_full_name("__rasg__b_0")]
    if not os.path.isfile(matrix_ref):
        deps.append(db.get_full_name(matrix_ref))
    return deps


@as_task(inputs=_kickoff_deps, priority=_DYNAMIC_TASK_PRIORITY)
def kickoff_dyn_task(db, matrix_ref, nsd, overlap_ratio, max_iter, tol, omega,
                     min_steps, sol_prefix, num_steps, update_rhs, gen):
    """coord 预分块（worker 上执行，master 零重活）+ 提交 step0 task 组。

    重投幂等：sub_{sd} 同参数同 hash 重写 → DUPLICATE_SKIPPED（值相同）。"""
    from .ras_graph_daemon import _coord_prebuild_pipeline

    _coord_prebuild_pipeline(db, matrix_ref, nsd, overlap_ratio, max_iter, tol,
                             omega, group_id=None, on_sub_ready=None)
    INFO(f"[RASG DYN KICKOFF] gen={gen} coord done, submitting step 0 group "
         f"(nsd={nsd} steps={num_steps})")
    _submit_step_group(db, 0, nsd, max_iter, tol, omega, min_steps,
                       sol_prefix, num_steps, update_rhs, gen)


def _submit_step_group(db, t, nsd, max_iter, tol, omega_strategy, min_steps,
                       sol_prefix, num_steps, update_rhs, gen):
    """提交时间步 t 的 task 组：compute×nsd（钉 sd_i worker）+ check（钉
    ras_check worker）。可在 master（kickoff API）或 worker（controller 链）
    上调用——后者经 Worker.submit 转发 master。"""
    group = PeerChannelGroup()
    for sd in range(nsd):
        compute_dyn_task(db, group.group_id, sd, nsd, t, sol_prefix, gen)
    check_dyn_task(db, group.group_id, nsd, t, max_iter, tol, omega_strategy,
                   min_steps, sol_prefix, num_steps, update_rhs, gen)


def _cached_read(db, name, cache_key):
    """读 db 对象并缓存反序列化结果到进程级 cache（大对象二次反序列化
    是 n=1000 的实测瓶颈：coord ~130ms）。gen 前缀的 cache_key 防跨 solve
    污染。"""
    from fly import get_cache, put_cache, has_cache
    if not has_cache(cache_key):
        put_cache(cache_key, db.read_object(name))
    return get_cache(cache_key)


def _compute_deps(db, group_id, sd, nsd, t, sol_prefix, gen):
    deps = [db.get_full_name(f"__rasg__b_{t}"),
            db.get_full_name(f"__rasg__sub_{sd}")]
    if t > 0:
        # warm start 权威源是持久对象 sol_{t-1}——显式依赖保证 restart 重投
        # 时 t 步必在上一步结果就绪后才调度。
        deps.append(db.get_full_name(f"{sol_prefix}_{t - 1}"))
    return deps


@as_task(inputs=_compute_deps,
         requires=lambda db, group_id, sd, nsd, t, sol_prefix, gen: [f"sd_{sd}"],
         priority=_DYNAMIC_TASK_PRIORITY)
def compute_dyn_task(db, group_id, sd, nsd, t, sol_prefix, gen):
    """时间步 t 的子域 compute 长 task：setup 缓存短路 → 读 b_t 切片 →
    warm start → RPC 迭代到 check 发 done。

    冷启动（进程缓存纯加速，db 是权威）：
    1. LDLT/setup：gen 前缀缓存短路 → miss 读 sub_{sd}（temp 落盘，restart
       后恢复可见）重建。
    2. warm start：sol_{t-1}（持久，restart 权威源）提取 ghost + 初值。
    3. b_t：temp 恢复。

    RPC 中断一律 raise（非 v2 的 break 干净退出）：task 失败语义使断链对
    failed_tasks.bin / wait_tasks 可见，避免"task 成功但链停滞"的静默挂死。
    """
    import numpy as np
    from _fly_solver import ex_slv_ras_bupdated_solve, EXSlvSubdomainSolver
    from fly import get_cache, put_cache, has_cache
    from core import get_config as _get_config

    # QA 失败注入钩子：FLY_RASG_FAIL_AT="t:sd" 使 compute(t, sd) 确定性失败
    # （验证组失败传染 + restart 断点续跑）。
    fail_at = os.environ.get("FLY_RASG_FAIL_AT")
    if fail_at is not None and fail_at == f"{t}:{sd}":
        raise RuntimeError(f"FLY_RASG_FAIL_AT injected at t={t} sd={sd}")

    cfg = _cached_read(db, "__rasg__cfg", f"__rasg__d_{gen}_cfg")
    tol = cfg["tol"]
    omega_strategy = cfg.get("omega", 1.0)

    # ── setup 缓存短路（worker 矩阵缓存复用的核心）──
    setup_key = f"__rasg__d_{gen}_setup_{sd}"
    solver_key = f"__rasg__d_{gen}_solver_{sd}"
    if has_cache(setup_key):
        setup = get_cache(setup_key)
        solver = get_cache(solver_key)
    else:
        sub = db.read_object(f"__rasg__sub_{sd}")
        openmp_threads = _get_config().get_int("solver_openmp_threads")
        if openmp_threads > 0:
            EXSlvSubdomainSolver.set_num_threads(openmp_threads)
        solver = EXSlvSubdomainSolver.from_coo(
            sub["size"], sub["a_rows"].tolist(), sub["a_cols"].tolist(),
            sub["a_vals"].tolist())
        setup = sub
        put_cache(setup_key, setup)
        put_cache(solver_key, solver)
        INFO(f"[RASG DYN COMPUTE] sd={sd} LDLT setup done (cold), "
             f"size={sub['size']}")

    # ── 当前时间步右端项（db 权威；b 每步不同，不进 setup 缓存）──
    b_t = np.asarray(db.read_object(f"__rasg__b_{t}"), dtype=np.float64)
    b_local = b_t[setup["local_indices"]]

    # ── warm start：sol_{t-1}（持久对象）提取 ghost 值 + 本子域初值 ──
    sol_prev = None
    warm_prev = None
    if t > 0:
        sol_prev = np.asarray(db.read_object(f"{sol_prefix}_{t - 1}"),
                              dtype=np.float64)
        # ghost：外部连接点的全局索引直接索引全局解（向量化）。
        # 本子域 primary 初值：local_indices[primary_local_pos] 是 primary
        # 点的全局索引，顺序与 x_primary 一致。
        warm_prev = sol_prev[setup["local_indices"][setup["primary_local_pos"]]]

    # ── Connect check（重投容错：旧 run 的 chan 地址可能残留，连死端口后
    # 重试等 check 重写新地址）──
    group = PeerChannelGroup(group_id)
    chan = _connect_with_retry(group, db)

    # ── RPC 迭代循环（v2 骨架；固定 b → b_local；warm start 参与 step=0 判定）──
    step = 0
    xc_cache = {}  # {sd: xc_array} 上轮粗校正后各子域的解（从 RPC 回复缓存）
    prev_x_key = f"__rasg__d_{gen}_prev_x_{sd}"
    while True:
        outside_coeffs = setup["outside_coeffs"]
        neighbor_values = np.zeros(len(outside_coeffs))
        if step == 0 and sol_prev is not None:
            neighbor_values = sol_prev[setup["outside_global_idx"]]
        elif step > 0:
            for nb_id in setup["neighbor_ids"]:
                if nb_id in xc_cache:
                    nb_x = xc_cache[nb_id]
                else:
                    nb_x = np.zeros(len(cfg["primary_sets"][nb_id]))
                recv_positions = setup["neighbor_recv_idx"][nb_id]
                conn_indices = setup["neighbor_needed"][nb_id]
                for i, conn_i in enumerate(conn_indices):
                    pos = recv_positions[i]
                    if 0 <= pos < len(nb_x):
                        neighbor_values[conn_i] = nb_x[pos]

        x_local = ex_slv_ras_bupdated_solve(
            solver, b_local,
            setup["outside_local_pos"], outside_coeffs,
            neighbor_values, 1.0)
        x_primary = np.asarray(x_local, dtype=np.float64)[setup["primary_local_pos"]]

        # 收敛判定：step>0 用上轮解；warm start 的 step=0 用上时间步收敛解。
        converged_local = False
        prev = None
        if step > 0 and has_cache(prev_x_key):
            prev = np.asarray(get_cache(prev_x_key), dtype=np.float64)
        elif step == 0 and warm_prev is not None:
            prev = warm_prev
        if prev is not None:
            converged_local = float(np.max(np.abs(x_primary - prev))) < tol
        put_cache(prev_x_key, x_primary)

        payload = pickle.dumps({
            "sd": sd, "step": step, "conv": converged_local,
            "x": serialize_array(x_primary),
        })
        try:
            status, resp = chan.rpc(payload, timeout=_RPC_TIMEOUT_SECONDS)
        except Exception as e:
            raise RuntimeError(
                f"[RASG DYN COMPUTE] sd={sd} t={t} RPC failed at step={step}: {e}")
        if status != PeerRpcStatus.OK:
            raise RuntimeError(
                f"[RASG DYN COMPUTE] sd={sd} t={t} check failure at "
                f"step={step} status={status}")

        result = pickle.loads(resp)
        if result["action"] == "done":
            INFO(f"[RASG DYN COMPUTE] sd={sd} t={t} done at step={step} "
                 f"conv={converged_local}")
            break

        if "xc_self" in result:
            xc_self = deserialize_array(result["xc_self"])
            put_cache(prev_x_key, xc_self)
            if "ghosts" in result:
                ghosts = pickle.loads(result["ghosts"])
                for nb_id_str, ghost_vals in ghosts.items():
                    nb_id = int(nb_id_str)
                    recv_positions = setup["neighbor_recv_idx"][nb_id]
                    if nb_id not in xc_cache:
                        xc_cache[nb_id] = np.zeros(len(cfg["primary_sets"][nb_id]))
                    nb_arr = xc_cache[nb_id]
                    for i, pos in enumerate(recv_positions):
                        if pos >= 0 and i < len(ghost_vals):
                            nb_arr[pos] = ghost_vals[i]
        step += 1

    chan.close()


def _connect_with_retry(group, db, total_timeout=60.0, attempt_timeout=5.0):
    """connect 容错重试：restart 重投场景下，db 里可能残留旧 run 的 chan
    地址对象（旧端口已死）——首次 connect 会失败，check 重投 listen 前
    会 remove 旧地址并写新值，这里重读重连直到窗口耗尽。"""
    deadline = time.monotonic() + total_timeout
    last_err = None
    while time.monotonic() < deadline:
        try:
            chan = group.connect(db, timeout=attempt_timeout)
            if chan is not None:
                return chan
        except Exception as e:
            last_err = e
        time.sleep(1.0)
    raise RuntimeError(
        f"connect to check failed within {total_timeout}s "
        f"(stale chan address from a previous run?): {last_err}")


def _check_deps(db, group_id, nsd, t, max_iter, tol, omega_strategy, min_steps,
                sol_prefix, num_steps, update_rhs, gen):
    deps = [db.get_full_name(f"__rasg__b_{t}")] + \
           [db.get_full_name(f"__rasg__sub_{s}") for s in range(nsd)]
    if omega_strategy == "coarse":
        deps.append(db.get_full_name("__rasg__coarse_prebuilt"))
    return deps


@as_task(inputs=_check_deps,
         requires=lambda db, group_id, nsd, t, max_iter, tol, omega_strategy,
                     min_steps, sol_prefix, num_steps, update_rhs, gen:
         ["ras_check"],
         priority=_DYNAMIC_TASK_PRIORITY)
def check_dyn_task(db, group_id, nsd, t, max_iter, tol, omega_strategy,
                   min_steps, sol_prefix, num_steps, update_rhs, gen):
    """时间步 t 的 check 长 task：listen → 收齐 nsd 份 → 内联粗校正 → 回复，
    循环到收敛。

    收敛路径顺序（组失败原子性的关键，与 v2 不同）：
        写 sol_t/iters_t/converged_t → 提交 controller(t) → respond done → 返回。
    check 的全部可观察副作用先于 respond done：check 在任何阶段失败时
    compute 侧 RPC 仍挂起 → task 结束（listener 关闭）后断连/超时 → compute
    raise → 整组进 failed_tasks.bin（重投单元完整）。
    """
    import scipy.sparse as sp
    from fly import get_cache, put_cache, has_cache

    group = PeerChannelGroup(group_id)
    # 重投场景：旧 run 残留的 chan 地址对象是旧端口（持久对象 + 同参数
    # DUPLICATE_SKIPPED 会保旧值）——先 remove（含 provenance erase）再
    # listen 写新地址，compute 侧 _connect_with_retry 配合重读。
    try:
        db.remove_object(group._temp_name())
    except Exception:
        pass
    listener = group.listen(db)
    INFO(f"[RASG DYN CHECK] t={t} listening port={listener.port}")

    coord = _cached_read(db, "__rasg__coord", f"__rasg__d_{gen}_coord")
    N = coord["N"]
    primary_sets = coord["primary_sets"]
    matrix_ref = coord["matrix_ref"]
    use_coarse = (omega_strategy == "coarse")

    # 当前时间步右端项（残差 r = b_t - A·x；v2 的固定 b 缓存不适用）
    b_t = np.asarray(db.read_object(f"__rasg__b_{t}"), dtype=np.float64)

    # ── 粗校正预构建（gen 前缀进程缓存，不走 ras_graph 的全局键——防跨
    # solve 污染；check worker 绑定保证跨时间步命中）。仅 prebuilt 快路径
    # （kickoff 的 pipeline 恒预构建），legacy 全量构建不支持。──
    Ac_lu = None
    P = None
    A_fine = None
    if use_coarse:
        lu_key = f"__rasg__d_{gen}_coarse_lu"
        if not has_cache(lu_key):
            from scipy import sparse
            from scipy.sparse.linalg import splu
            prebuilt = db.read_object("__rasg__coarse_prebuilt")
            if prebuilt is None or prebuilt.get("skip") or "Ac_indptr" not in prebuilt:
                raise RuntimeError(
                    "[RASG DYN CHECK] coarse prebuilt missing — dynamic mode "
                    "requires the coord prebuild fast path")
            t0 = time.perf_counter()
            put_cache(f"__rasg__d_{gen}_coarse_P", sparse.csr_matrix(
                (prebuilt["P_vals"], (prebuilt["P_rows"], prebuilt["P_cols"])),
                shape=(prebuilt["N"], prebuilt["Nc"])))
            put_cache(lu_key, splu(sparse.csc_matrix(
                (prebuilt["Ac_data"], prebuilt["Ac_indices"],
                 prebuilt["Ac_indptr"]), shape=prebuilt["Ac_shape"])))
            INFO(f"[RASG DYN CHECK] coarse LU built from prebuilt: "
                 f"Nc={prebuilt['Nc']} t={(time.perf_counter()-t0)*1000:.0f}ms")
        Ac_lu = get_cache(lu_key)
        P = get_cache(f"__rasg__d_{gen}_coarse_P")

        a_key = f"__rasg__d_{gen}_coarse_A"
        if not has_cache(a_key):
            from .ras_graph import _get_matrix_data
            md = _get_matrix_data(matrix_ref, db)
            A_fine = sp.csr_matrix(
                (np.asarray(md["vals"]), (np.asarray(md["rows"]), np.asarray(md["cols"]))),
                shape=(N, N))
            put_cache(a_key, A_fine)
        else:
            A_fine = get_cache(a_key)

    ps_arrays = [np.asarray(ps) for ps in primary_sets]

    # check 侧 sub 缓存（neighbor_recv_idx 用于 ghosts 提取；gen 前缀防跨
    # solve 污染）
    sub_cache_key = f"__rasg__d_{gen}_sub_cache"
    if not has_cache(sub_cache_key):
        sub_cache = {}
        for sd_idx in range(nsd):
            sub_cache[sd_idx] = db.read_object(f"__rasg__sub_{sd_idx}")
        put_cache(sub_cache_key, sub_cache)
    sub_cache = get_cache(sub_cache_key)

    step = 0
    while step < max_iter:
        # ── 收齐 nsd 份（按 sd 去重收满；超时 raise）──
        # 断连事件一律吞掉继续收：PeerRpc 事件队列是 agent 级的，同 worker
        # 上前一个 check task 的 listener.close()（stop server 关闭残留连接）
        # 会向队列投放断连事件，被本组 accept_one 误读为致命错误（实测：
        # t=0 收敛后 10ms 内 t=1 check 即被陈旧事件炸死）。组内真实断连的
        # 失败语义由"收不齐 + 超时"兜底，不影响原子传染。
        contributions = {}
        collect_deadline = time.monotonic() + _RPC_TIMEOUT_SECONDS
        while len(contributions) < nsd:
            if time.monotonic() > collect_deadline:
                raise RuntimeError(
                    f"[RASG DYN CHECK] t={t} timed out waiting for "
                    f"contributions {len(contributions)}/{nsd} at step={step}")
            try:
                conn_id, rpc_id, src, payload = listener.accept_one(timeout=5.0)
            except RuntimeError as e:
                DBG(f"[RASG DYN CHECK] t={t} skipping disconnect event "
                    f"at step={step}: {e}")
                continue
            if conn_id == 0 and rpc_id == 0:
                continue  # 单次 5s 无事件，外层 deadline 控制总窗口
            data = pickle.loads(payload)
            contributions[data["sd"]] = {
                "conn_id": conn_id, "rpc_id": rpc_id,
                "x": deserialize_array(data["x"]), "conv": data["conv"],
            }
        all_converged = all(c["conv"] for c in contributions.values())

        # check 侧残差兜底（compute 的 conv 可能因粗校正干扰不触发）；
        # step < min_steps 强制不收敛（防早期残差假小，v2 硬编码 5 的参数化）。
        x_global = np.zeros(N, dtype=np.float64)
        for sd_idx in range(nsd):
            x_global[ps_arrays[sd_idx]] = contributions[sd_idx]["x"]
        if use_coarse and Ac_lu is not None:
            r_norm = float(np.linalg.norm(b_t - A_fine.dot(x_global)))
            r_rel = r_norm / max(float(np.linalg.norm(b_t)), 1e-30)
            if step >= min_steps and r_rel < tol:
                all_converged = True
        if step < min_steps:
            all_converged = False

        if all_converged or step == max_iter - 1:
            # ── 收敛：respond done 先行（reactor 异步 send 需缓冲期刷出，
            # 紧随的 listener.close() 会掐死未发出的 respond——v2 同款顺序，
            # 写库/提交 controller 的同步往返就是天然缓冲），再落库与链推进。
            # 原子性权衡：respond 后写库失败 → compute 已成功退出、bin 只剩
            # check，重投会因收不齐超时失败可见（不静默），与 v2 风险面一致。──
            for sd in range(nsd):
                c = contributions[sd]
                try:
                    listener.respond(c["conn_id"], c["rpc_id"],
                                     pickle.dumps({"action": "done"}))
                except Exception:
                    pass
            db.write_object(f"{sol_prefix}_{t}", x_global)
            db.write_object(f"__rasg__iters_{t}", step + 1, save_to_db=False)
            db.write_object(f"__rasg__converged_{t}", all_converged,
                            save_to_db=False)
            controller_dyn_task(db, t, nsd, max_iter, tol, omega_strategy,
                                min_steps, num_steps, update_rhs, sol_prefix,
                                group_id, gen)
            INFO(f"[RASG DYN CHECK] t={t} converged={all_converged} "
                 f"at step={step}")
            break

        # ── 内联粗校正（完全在内存，不经 DB；残差用当前步 b_t）──
        if use_coarse and Ac_lu is not None:
            r = b_t - A_fine.dot(x_global)
            e_c = Ac_lu.solve(P.T.dot(r))
            x_corrected = x_global + P.dot(e_c)
        else:
            x_corrected = x_global

        xc_primary = {}
        for sd in range(nsd):
            xc_primary[sd] = x_corrected[ps_arrays[sd]]

        for sd in range(nsd):
            c = contributions[sd]
            sub = sub_cache[sd]
            payload = {"action": "continue",
                       "xc_self": serialize_array(xc_primary[sd])}
            ghosts = {}
            for nb_id in sub["neighbor_ids"]:
                recv_positions = sub["neighbor_recv_idx"][nb_id]
                nb_xc = xc_primary[nb_id]
                ghosts[nb_id] = [float(nb_xc[pos])
                                 if 0 <= pos < len(nb_xc) else 0.0
                                 for pos in recv_positions]
            payload["ghosts"] = pickle.dumps(ghosts)
            listener.respond(c["conn_id"], c["rpc_id"], pickle.dumps(payload))
        step += 1

    listener.close()


@as_task(inputs=lambda db, t, nsd, max_iter, tol, omega_strategy, min_steps,
                     num_steps, update_rhs, sol_prefix, prev_group_id, gen:
         [db.get_full_name(f"__rasg__converged_{t}")],
         priority=_DYNAMIC_TASK_PRIORITY)
def controller_dyn_task(db, t, nsd, max_iter, tol, omega_strategy, min_steps,
                        num_steps, update_rhs, sol_prefix, prev_group_id, gen):
    """时间步控制器：依赖 converged_t（调度器保证 t 步完成后才运行），
    决定是否启动下一个时间步——master 上永远不做阻塞式流程的链式替代。

    终止条件：t+1 >= num_steps 或 update_rhs 返回 None → 写 dynamic_done。
    否则：update_rhs 生成 b_{t+1} → 提交 step t+1 组（check(t+1) 将提交
    controller(t+1)，链闭合）→ 清理本步中间量（b_t / chan 地址）。
    """
    x_t = db.read_object(f"{sol_prefix}_{t}")

    # 清理本步 chan 地址对象（check(t) 的 RPC 已结束；组失败时本 task 不
    # 会运行——converged_t 未就绪，chan 保留供重投的 check remove 重写）。
    try:
        db.remove_object(f"__fly_chan_{prev_group_id}")
    except Exception:
        pass

    if t + 1 >= num_steps:
        _write_dynamic_done(db, t, sol_prefix)
        return

    b_next = update_rhs(x_t, t + 1)
    if b_next is None:
        INFO(f"[RASG DYN CTRL] t={t} update_rhs returned None, stopping early")
        _write_dynamic_done(db, t, sol_prefix)
        return

    db.write_object(f"__rasg__b_{t + 1}", b_next, save_to_db=False)
    INFO(f"[RASG DYN CTRL] t={t} → t={t + 1} rhs updated, submitting next group")

    _submit_step_group(db, t + 1, nsd, max_iter, tol, omega_strategy,
                       min_steps, sol_prefix, num_steps, update_rhs, gen)

    # 清理 b_t：t 步组已收敛消费完毕；组失败时 controller 不会运行（依赖
    # converged_t），b_t 保留供重投的 compute 使用——时序上重投窗口安全。
    try:
        db.remove_object(f"__rasg__b_{t}")
    except Exception:
        pass


def _write_dynamic_done(db, last_t, sol_prefix):
    iters = [db.read_object(f"__rasg__iters_{i}") for i in range(last_t + 1)]
    conv = [db.read_object(f"__rasg__converged_{i}") for i in range(last_t + 1)]
    db.write_object("__rasg__dynamic_done", {
        "num_steps_done": last_t + 1,
        "iters": iters,
        "converged": conv,
        "sol_names": [f"{sol_prefix}_{i}" for i in range(last_t + 1)],
    }, save_to_db=False)
    INFO(f"[RASG DYN CTRL] dynamic done: steps={last_t + 1} iters={iters}")


@wait_obj(inputs=lambda db, timeout: [db.get_full_name("__rasg__dynamic_done")])
def get_dynamic_result(db, timeout=None):
    """等待整体完成（用户自主决定何时阻塞）。返回 dynamic_done 汇总：
    {"num_steps_done", "iters", "converged", "sol_names"}。"""
    return db.read_object("__rasg__dynamic_done")
