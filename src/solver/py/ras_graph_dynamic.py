"""Dynamic 多右端项连续求解（EmIR dynamic IR drop 场景）——三阶段架构。

同一矩阵 G 连续求解 T 个时间步的 G·x_t = b_t（b_t = f(x_{t-1})，严格串行）。

  阶段 1 setup（每链一次，跨全部时间步复用；RPC 连接方向：compute listen /
           check 主动连接——成员缺失场景全部有确定性出口，见"失败语义"）
    setup_compute × nsd (requires=worker_attr("sd_i"))
      LDLT/子域数据 → 进程缓存（key 按 matrix_ref：重投/换代不重做分解）
      stop 旧 server → listen 新端口 → 端口入缓存（key 按 gen）
      写地址对象 addr_{gen}_{sd}（temp；同时是下游依赖锚）
      set_worker_property(worker_attr("{gen}_{sd}"))
    setup_check (requires=worker_attr("check"), inputs=全部 addr + b_{start_t})
      粗校正 LU/A_fine/子域索引 → 缓存（key 按 matrix_ref）
      connect × nsd → 池 {sd: conn_id} 入缓存
      set_worker_property(worker_attr("{gen}_check")) → 提交 solver(start_t)

  阶段 2 solver per t（task 隔离保持）
    compute(t,sd)  被动循环：recv_request → 本地 solve → respond 贡献；
                   done 请求 → ack 退出
    check(t)       驱动循环：逐存活成员 call（请求带 ghosts）→ 收贡献 →
                   残差主导收敛判定 / 粗校正 → 下轮；收敛发 done → 写
                   sol_t/iters_t/converged_t → 提交 controller(t)

  阶段 3 controller(t) (requires=worker_attr("check")，与 check 同 worker)
    有下一步：update_rhs(x_t, t+1) → 写 b_{t+1} → 提交 solver(t+1)（全量复用）
    无下一步：本 worker 收尾（关池/清粗校正缓存/移除属性）→ 发 cleanup × nsd
              （各 compute worker 销毁矩阵/listener、关 server、移除属性、
              删地址对象）→ 写 dynamic_done

编队属性命名（SolveDb.worker_attr 单点生成，并发 flow 隔离）：
    worker 属性与 task requires 统一经 db.worker_attr(tag) =
    "rasg:{db_uid}:{tag}"（uid 跨进程持久于 _DB_META）。并发求解 flow 各持
    不同 uid → 属性零交集，调度精确匹配不串池；kickoff 用 ensure_workers
    申请编队时 exclude=r"^rasg:" 排除已被其他 flow 占用的 worker。

失败语义（全部事件/依赖驱动，无任何等待窗口——timeout 裁定）：
    - 成员 setup 失败（含 listen 失败）：addr 缺失 → setup_check inputs
      依赖不可解 → fail_unscheduleable_tasks 连锁 → 整组进 bin；
    - 成员在 setup_check connect 前死亡：connect 被拒即时返回 → raise 同上；
    - solver 中成员死/task 异常退出（except 强关 server）：check 的 call
      立即 FAILED（断连事件唤醒），alive 不变式 <nsd → 组死；
    - check 死/task 异常退出：stop_peer_rpc 强关连接，全体成员挂起的
      recv_request 被错误断连唤醒 → 全员失败；
    - 集群关机：agent.is_running() 全路径检查。

重投语义（用户裁定）：
    compute 重投 → listener 缓存 miss（组散时已清）→ 直接 return（no-op，
    真组由新链发出）；check 重投 → 池缓存 miss → 判定组散 → 以新 gen 重新
    驱动 setup 链（LDLT 数据跨代命中秒过、仅连接重建）→ 新 setup_check 提交
    solver(start_t=失败步)。旧 gen 的 compute task 被 setup 的 stop 旧 server
    唤醒退出，最坏一轮涟漪后二次重投全部 no-op 收敛。
    注意 update_rhs 须为确定性回调（重投会重调）；kickoff 起点固定为 t=0。

对象命名（provenance：跨步对象带 t；写前 remove 清旧代残留，防换 gen 后
参数 hash 不同被 DUPLICATE/PROVENANCE 拒绝）：
    __rasg__b_{t}                        temp   controller(t-1) 写，controller(t) 删
    __rasg__d_addr_{gen}_{sd}            temp   setup_compute 写（依赖锚 + 地址）
    __rasg__sub_{sd}/coord/cfg/coarse_prebuilt  temp   kickoff 写，全程
    __rasg__sol_{t}                      持久   check 写（用户数据）
    __rasg__iters_{t} / converged_{t}    temp   check 写（controller 锚点）
    __rasg__dynamic_done                 temp   终止 controller 写（等待点）

进程级缓存 key：
    连接对象按 gen（__rasg__d_listener_{gen}_{sd} = port、
    __rasg__d_pool_{gen} = {sd: conn_id}）——换代即重建、隔离重投窗口；
    数据按 matrix_ref（__rasg__d_setup/solver_{matrix_ref}_{sd}、
    __rasg__d_coarse_*、__rasg__d_sub_cache_{matrix_ref}）——LDLT 只做一次。
"""
import os
import time
import pickle
import uuid
import numpy as np

from _fly_log import DBG, INFO, WARN, ERR
from fly import as_task, wait_obj
from agent import serialize_array, deserialize_array

# PeerRpcStatus 数值常量（peer_rpc_call 返回值）：0 PENDING / 1 OK /
# 2 ERROR / 3 FAILED。直接用数值避免 agent 包内包装类耦合。
_RPC_OK = 1
# task 组优先级：高于默认 10——与集群其他任务共存时优先获得 idle worker。
_DYNAMIC_TASK_PRIORITY = 90


def _remove_quiet(db, name):
    try:
        db.remove_object(name)
    except Exception:
        pass


# ───────────────────────── Public API ─────────────────────────

def solve_ras_graph_dynamic(db, matrix_ref, nsd, b0, update_rhs, num_steps,
                            overlap_ratio=0.50, max_iter=100, tol=1e-8,
                            omega=1.0, min_steps=2, sol_prefix="__rasg__sol",
                            max_concurrent_compute=None):
    """Dynamic 多右端项 kickoff（非阻塞，立即返回）。

    Args:
        db: Database（矩阵对象应已写入；同 db 重复调用需换 sol_prefix 或
            先清历史对象——sol_t 同名同参数重写会被 provenance 跳过）。
        matrix_ref: 矩阵对象名（推荐）或 .npz 文件路径（本地实验）。
        nsd: 子域数。
        b0: 初始右端项（np.ndarray）。
        update_rhs: Callable[[np.ndarray, int], np.ndarray | None]——在
            controller task 内执行：输入上一步全局解 x_{t-1} 与下一步号 t，
            返回 b_t；返回 None 提前终止。**须确定性**（重投会重调）。
        num_steps: 时间步总数（含 t=0）。
        min_steps: 每步最少迭代数（防冷启动早期残差假小）。
        sol_prefix: 结果对象名前缀。

    Returns:
        dict(sol_prefix, num_steps, db_path, gen)。get_dynamic_result(db)
        等待整体完成并取汇总；单步结果随时 db.read_object(f"{sol_prefix}_{t}")。
    """
    from fly.runtime import get_agent

    if num_steps < 1:
        raise ValueError(f"num_steps must be >= 1, got {num_steps}")

    n_workers = min(nsd, max_concurrent_compute) if max_concurrent_compute else nsd
    agent = get_agent()
    # 编队申请：nsd 个 compute 绑定（sd 属性轮转分组，钉住进程缓存）+ 1 个
    # check/controller 宿主。属性统一经 db.worker_attr（rasg:{uid}: 命名空间，
    # 并发 flow 不串池）；ensure_workers 幂等盘点已满足的绑定、缺失的从现有
    # 空闲 worker 追加属性补齐（exclude 排除已被其他 flow 编队的 worker）。
    attrs = [db.worker_attr(f"sd_{s}") for s in range(nsd)]
    request = [[attrs[s] for s in range(nsd) if s % n_workers == w]
               for w in range(n_workers)]
    request.append(db.worker_attr("check"))

    if hasattr(agent, "worker_count"):
        # master 上下文（QA 脚本直接调 solve_once/solve_ras_graph_dynamic）：
        # 进程数量先行（ensure_workers 只分配现有 worker 不启动新进程）：总数
        # 不够时按缺口补空属性 worker。注册就绪不在此等待——ensure 把已唤起
        # 未注册的占位符计入预检容量，注册等待受声明的 timeout 约束（存活池
        # 已满足时零等待立即返回）。
        have = agent.worker_count if agent.is_running() else 0
        if not agent.is_running() or have < len(request):
            deficit = len(request) - max(0, have)
            agent.launch_local_workers([{} for _ in range(deficit)])
        agent.ensure_workers(request, timeout=10.0, exclude=r"^rasg:")
    # worker 上下文（flows._solve_kickoff_task 在 worker 进程执行，非阻塞提
    # 交）：编队由 flow 侧（master）的 ensure_workers 负责，此处不自举属性
    # ——kickoff 执行 worker 若自举 check 会与阻塞链自等死锁。

    gen = uuid.uuid4().hex[:8]
    INFO(f"[RASG DYN] kickoff: gen={gen} nsd={nsd} n_workers={n_workers + 1} "
         f"steps={num_steps} omega={omega} min_steps={min_steps}")

    _remove_quiet(db, "__rasg__dynamic_done")

    db.write_object("__rasg__b_0", b0, save_to_db=False)

    kickoff_dyn_task(db, matrix_ref, nsd, overlap_ratio, max_iter, tol, omega,
                     min_steps, sol_prefix, num_steps, update_rhs, gen)
    return {"sol_prefix": sol_prefix, "num_steps": num_steps,
            "db_path": db.get_db_path(), "gen": gen}


@wait_obj(inputs=lambda db: [db.get_full_name("__rasg__dynamic_done")])
def get_dynamic_result(db):
    """等待整体完成并返回汇总（timeout 裁定：数据规模相关等待不设窗口，
    失败语义由 wait_obj 的 can_still_produce 兜底）：
    {"num_steps_done", "iters", "converged", "sol_names"}。"""
    return db.read_object("__rasg__dynamic_done")


# ───────────────────── 缓存 key（分层：数据按矩阵、连接按代） ─────────────────────

def _data_cache_keys(matrix_ref, sd):
    return (f"__rasg__d_setup_{matrix_ref}_{sd}",
            f"__rasg__d_solver_{matrix_ref}_{sd}")


def _check_data_cache_keys(matrix_ref):
    return (f"__rasg__d_coarse_lu_{matrix_ref}",
            f"__rasg__d_coarse_P_{matrix_ref}",
            f"__rasg__d_coarse_A_{matrix_ref}",
            f"__rasg__d_sub_cache_{matrix_ref}")


# ───────────────────── 阶段 0：kickoff（coord 预分块） ─────────────────────

def _kickoff_deps(db, matrix_ref, nsd, overlap_ratio, max_iter, tol, omega,
                  min_steps, sol_prefix, num_steps, update_rhs, gen):
    deps = [db.get_full_name("__rasg__b_0")]
    if not os.path.isfile(matrix_ref):
        deps.append(db.get_full_name(matrix_ref))
    return deps


@as_task(inputs=_kickoff_deps, priority=_DYNAMIC_TASK_PRIORITY)
def kickoff_dyn_task(db, matrix_ref, nsd, overlap_ratio, max_iter, tol, omega,
                     min_steps, sol_prefix, num_steps, update_rhs, gen):
    """coord 预分块（worker 上执行，master 零重活）→ 提交 setup 链。
    重投幂等：sub_{sd} 同参数同 hash 重写 → DUPLICATE_SKIPPED（值相同）。"""
    _coord_prebuild_pipeline(db, matrix_ref, nsd, overlap_ratio, max_iter, tol,
                             omega, group_id=None, on_sub_ready=None)
    INFO(f"[RASG DYN KICKOFF] gen={gen} coord done, submitting setup chain")
    _submit_setup_chain(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                        max_iter, tol, omega, min_steps, gen, start_t=0)


def _submit_setup_chain(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                        max_iter, tol, omega, min_steps, gen, start_t):
    """setup_compute × nsd → setup_check(start_t)。首轮由 kickoff 调用
    （start_t=0）；重投由判定组散的 check 调用（新 gen，start_t=失败步）。"""
    for sd in range(nsd):
        setup_compute_task(db, matrix_ref, nsd, sd, gen)
    setup_check_task(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                     max_iter, tol, omega, min_steps, gen, start_t)


def _submit_solver_group(db, matrix_ref, nsd, sol_prefix, num_steps,
                         update_rhs, max_iter, tol, omega, min_steps, gen, t):
    for sd in range(nsd):
        compute_dyn_task(db, matrix_ref, nsd, sd, sol_prefix, gen, t)
    check_dyn_task(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                   max_iter, tol, omega, min_steps, gen, t)


# ───────────────────── 阶段 1：setup（RPC + 矩阵，一次建立全程复用） ─────────────────────

@as_task(inputs=lambda db, matrix_ref, nsd, sd, gen: [
             db.get_full_name(f"__rasg__sub_{sd}")],
         requires=lambda db, matrix_ref, nsd, sd, gen: [db.worker_attr(f"sd_{sd}")],
         priority=_DYNAMIC_TASK_PRIORITY)
def setup_compute_task(db, matrix_ref, nsd, sd, gen):
    """compute worker 的 setup：矩阵初始化（幂等）+ RPC service 初始化
    （每代干净重建）+ 写代际地址 + 设调度可见属性。

    失败的任何一步都导致 addr 缺失 → setup_check inputs 依赖不可解 →
    fail_unscheduleable_tasks 连锁失败（整组进 bin，可 restart）。"""
    from _fly_solver import EXSlvSubdomainSolver
    from fly import put_cache, has_cache
    from fly.runtime import get_agent
    from core import get_config as _get_config

    agent = get_agent()
    cfg = db.read_object("__rasg__cfg")

    # ── 矩阵初始化（数据 key 按 matrix_ref：命中则跳过 LDLT）──
    setup_key, solver_key = _data_cache_keys(matrix_ref, sd)
    if not has_cache(setup_key):
        sub = db.read_object(f"__rasg__sub_{sd}")
        openmp_threads = _get_config().get_int("solver_openmp_threads")
        if openmp_threads > 0:
            EXSlvSubdomainSolver.set_num_threads(openmp_threads)
        solver = EXSlvSubdomainSolver.from_coo(
            sub["size"], sub["a_rows"].tolist(), sub["a_cols"].tolist(),
            sub["a_vals"].tolist())
        from fly import put_cache as _pc
        _pc(setup_key, sub)
        _pc(solver_key, solver)
        INFO(f"[RASG DYN SETUP] sd={sd} LDLT done (cold), size={sub['size']}")

    # ── RPC service（每代干净重建：stop 旧 server 使挂起的旧 gen compute
    # 的 recv_request 被错误断连唤醒退出，防止新旧 task 抢同一请求队列）──
    agent.stop_peer_rpc()
    port = agent.start_peer_rpc_listen("127.0.0.1", 0)
    if port <= 0:
        raise RuntimeError(f"[RASG DYN SETUP] sd={sd} listen failed")

    # 该子域的服务端唯一消费者 = 常驻线程（agent 级请求队列全生命周期只能
    # 有一个 reader——短命 task 各自 recv 会互抢错账，实测事故）。solver(t)
    # task 通过 shared dict 注入当前步参数（b_local 等），线程按请求取出
    # 使用；done 只标记本步结束（线程不退，供下一时间步复用）；teardown 置
    # stop 后关 server 兜底唤醒。
    from fly import put_cache as _pc
    shared = {"stop": False, "poison": None,
              "step_ctx": None,      # {"b_local", "tol"} 由 compute(t) 注入
              "prev_x": None}
    _pc(f"__rasg__d_svc_{gen}_{sd}", shared)

    import threading
    threading.Thread(
        target=_serve_loop, args=(agent, f"__rasg__d_solver_{matrix_ref}_{sd}",
                                  f"__rasg__d_svc_{gen}_{sd}", sd),
        name=f"rasg-dyn-{gen}-{sd}", daemon=True).start()

    # 代际地址对象：依赖锚（本 task 失败则它缺失，下游连锁）+ 地址载体
    db.write_object(f"__rasg__d_addr_{gen}_{sd}",
                    {"host": "127.0.0.1", "port": port}, save_to_db=False)

    # worker 属性：本 worker 已完成该代该子域 setup（调度可见事实，收尾移除）
    agent.set_worker_property(db.worker_attr(f"{gen}_{sd}"))
    INFO(f"[RASG DYN SETUP] sd={sd} gen={gen} listening port={port}")


def _serve_loop(agent, solver_key, shared_key, sd):
    """成员子域的唯一常驻消费者：收请求(check 驱动) → 以 shared['step_ctx']
    中的当前步参数本地 solve → 回贡献。done=本步结束信号（ack 后继续候下
    一步）。存活期 = 整个动态链；异常退出仅发生在关机/server 强关时。"""
    import numpy as np
    from _fly_solver import ex_slv_ras_bupdated_solve
    from fly import get_cache, has_cache

    while has_cache(shared_key) and not get_cache(shared_key)["stop"]:
        try:
            conn_id, rpc_id, src, payload = agent.peer_rpc_recv_request(0)
        except Exception as e:
            if has_cache(shared_key) and not get_cache(shared_key)["stop"]:
                ERR(f"[RASG DYN SVC] sd={sd} recv failed: {e}")
            break   # server 被关（正常收尾或强杀）
        try:
            data = pickle.loads(payload)
            shared = get_cache(shared_key)
            if data["action"] == "done":
                agent.peer_rpc_respond(conn_id, rpc_id,
                                       pickle.dumps({"ack": True}))
                shared["prev_x"] = None
                INFO(f"[RASG DYN SVC] sd={sd} step-group finished at "
                     f"step={data.get('step')}")
                continue
            ctx = shared["step_ctx"]
            if ctx is None or data.get("fail_hint"):
                agent.peer_rpc_respond_failure(conn_id, rpc_id, b"no ctx")
                continue

            if shared.get("poison"):
                raise RuntimeError(str(shared["poison"]))

            ghosts = deserialize_array(data["ghosts"])
            x_local = ex_slv_ras_bupdated_solve(
                get_cache(solver_key), ctx["b_local"],
                ctx["outside_local_pos"], ctx["outside_coeffs"],
                ghosts, 1.0)
            x_primary = np.asarray(x_local, dtype=np.float64)[
                ctx["primary_local_pos"]]
            conv = False
            if shared.get("prev_x") is not None:
                conv = float(np.max(np.abs(x_primary - shared["prev_x"]))) < \
                    ctx["tol"]
            shared["prev_x"] = x_primary
            agent.peer_rpc_respond(conn_id, rpc_id, pickle.dumps({
                "x": serialize_array(x_primary), "conv": conv}))
        except Exception as e:
            WARN(f"[RASG DYN SVC] sd={sd} request failed: {e}")
            try:
                agent.peer_rpc_respond_failure(conn_id, rpc_id,
                                               str(e)[:200].encode())
            except Exception:
                pass


def _setup_check_deps(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                      max_iter, tol, omega, min_steps, gen, start_t):
    return [db.get_full_name(f"__rasg__d_addr_{gen}_{s}") for s in range(nsd)] + \
           [db.get_full_name(f"__rasg__b_{start_t}")]


@as_task(inputs=_setup_check_deps,
         requires=lambda db, matrix_ref, nsd, sol_prefix, num_steps,
                     update_rhs, max_iter, tol, omega, min_steps, gen,
                     start_t: [db.worker_attr("check")],
         priority=_DYNAMIC_TASK_PRIORITY)
def setup_check_task(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                     max_iter, tol, omega, min_steps, gen, start_t):
    """check worker 的 setup：粗校正初始化（幂等）+ connect 全部成员 +
    提交 solver(start_t) 组。

    connect 被拒（成员 setup 后已死）在此即 raise——check_ready 缺失，
    下游连锁失败进 bin。"""
    import scipy.sparse as sp
    from fly import get_cache, put_cache, has_cache
    from fly.runtime import get_agent

    pool_key = f"__rasg__d_pool_{gen}"
    if has_cache(pool_key):
        # 防御分支：正常流程中新 gen 必 miss。
        INFO(f"[RASG DYN SETUP CHECK] gen={gen} pool cached, "
             f"submitting solver({start_t})")
        _submit_solver_group(db, matrix_ref, nsd, sol_prefix, num_steps,
                             update_rhs, max_iter, tol, omega, min_steps,
                             gen, start_t)
        return

    agent = get_agent()
    coord = db.read_object("__rasg__coord")
    N = coord["N"]
    use_coarse = (omega == "coarse")

    # ── 粗校正数据（key 按 matrix_ref：跨代共享）──
    lu_key, P_key, A_key, sub_key = _check_data_cache_keys(matrix_ref)
    if use_coarse and not has_cache(lu_key):
        from scipy import sparse
        from scipy.sparse.linalg import splu
        prebuilt = db.read_object("__rasg__coarse_prebuilt")
        if prebuilt is None or prebuilt.get("skip") or "Ac_indptr" not in prebuilt:
            raise RuntimeError(
                "[RASG DYN SETUP CHECK] coarse prebuilt missing — dynamic "
                "coarse mode requires the coord prebuild fast path")
        t0 = time.perf_counter()
        put_cache(P_key, sparse.csr_matrix(
            (prebuilt["P_vals"], (prebuilt["P_rows"], prebuilt["P_cols"])),
            shape=(prebuilt["N"], prebuilt["Nc"])))
        put_cache(lu_key, splu(sparse.csc_matrix(
            (prebuilt["Ac_data"], prebuilt["Ac_indices"],
             prebuilt["Ac_indptr"]), shape=prebuilt["Ac_shape"])))
        INFO(f"[RASG DYN SETUP CHECK] coarse LU built: Nc={prebuilt['Nc']} "
             f"t={(time.perf_counter()-t0)*1000:.0f}ms")
    if not has_cache(A_key):
        md = _get_matrix_data(matrix_ref, db)
        put_cache(A_key, sp.csr_matrix(
            (np.asarray(md["vals"]), (np.asarray(md["rows"]), np.asarray(md["cols"]))),
            shape=(N, N)))
    if not has_cache(sub_key):
        put_cache(sub_key, {s: db.read_object(f"__rasg__sub_{s}")
                            for s in range(nsd)})

    # ── RPC client：connect 全部成员（被拒 = 该成员 setup 后已死）──
    pool = {}
    for sd in range(nsd):
        addr = db.read_object(f"__rasg__d_addr_{gen}_{sd}")
        conn_id = agent.peer_rpc_connect(addr["host"], addr["port"])
        if conn_id == 0:
            raise RuntimeError(
                f"[RASG DYN SETUP CHECK] connect sd={sd} refused "
                f"({addr['host']}:{addr['port']}) — member dead after setup")
        pool[sd] = conn_id
    put_cache(pool_key, pool)
    agent.set_worker_property(db.worker_attr(f"{gen}_check"))
    INFO(f"[RASG DYN SETUP CHECK] gen={gen} pool of {nsd} connected, "
         f"submitting solver({start_t})")

    _submit_solver_group(db, matrix_ref, nsd, sol_prefix, num_steps,
                         update_rhs, max_iter, tol, omega, min_steps,
                         gen, start_t)


# ───────────────────── 阶段 2：solver（per t，被动/驱动对偶） ─────────────────────

def _compute_deps(db, matrix_ref, nsd, sd, sol_prefix, gen, t):
    return [db.get_full_name(f"__rasg__b_{t}"),
            db.get_full_name(f"__rasg__d_addr_{gen}_{sd}")]


@as_task(inputs=_compute_deps,
         requires=lambda db, matrix_ref, nsd, sd, sol_prefix, gen, t: [db.worker_attr(f"sd_{sd}")],
         priority=_DYNAMIC_TASK_PRIORITY)
def compute_dyn_task(db, matrix_ref, nsd, sd, sol_prefix, gen, t):
    """本时间步的参数注入：把当前步的 b_local / warm-start 基准写入成员
    service 线程的 shared 结构后立即返回（真正的迭代消费在 setup 启动的
    常驻线程中，跨时间步复用；task 短小，避免多消费者抢队列——实测事故
    教训）。

    重投语义（裁定）：svc 缓存 miss（组散/换代时已清）→ 直接 return。

    QA 注入钩子：FLY_RASG_FAIL_AT="t:sd" 时置 poison——service 线程对该
    成员的下一请求返回 failure → check 断言组死。"""
    import numpy as np
    from fly import get_cache, has_cache

    shared_key = f"__rasg__d_svc_{gen}_{sd}"
    if not has_cache(shared_key):
        INFO(f"[RASG DYN COMPUTE] sd={sd} t={t} gen={gen} stale task, no-op "
             f"(fresh group will be issued)")
        return

    cfg = db.read_object("__rasg__cfg")
    setup_key, _solver_key = _data_cache_keys(matrix_ref, sd)
    setup = get_cache(setup_key)

    b_t = np.asarray(db.read_object(f"__rasg__b_{t}"), dtype=np.float64)
    b_local = b_t[setup["local_indices"]]

    shared = get_cache(shared_key)
    shared["step_ctx"] = {
        "b_local": b_local,
        "outside_local_pos": setup["outside_local_pos"],
        "outside_coeffs": setup["outside_coeffs"],
        "primary_local_pos": setup["primary_local_pos"],
        "tol": cfg["tol"],
    }
    # QA 注入：置毒后 service 线程对本成员请求回 failure → check 组死连锁
    fail_at = os.environ.get("FLY_RASG_FAIL_AT")
    if fail_at is not None and fail_at == f"{t}:{sd}":
        shared["poison"] = f"FLY_RASG_FAIL_AT injected at t={t} sd={sd}"
        INFO(f"[RASG DYN COMPUTE] sd={sd} t={t} poison armed")


@as_task(inputs=lambda db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                     max_iter, tol, omega, min_steps, gen, t: [
         db.get_full_name(f"__rasg__b_{t}")] +
         ([db.get_full_name(f"{sol_prefix}_{t - 1}")] if t > 0 else []),
         requires=lambda db, matrix_ref, nsd, sol_prefix, num_steps,
                     update_rhs, max_iter, tol, omega, min_steps, gen,
                     t: [db.worker_attr("check")],
         priority=_DYNAMIC_TASK_PRIORITY)
def check_dyn_task(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                   max_iter, tol, omega, min_steps, gen, t):
    """驱动迭代：逐存活成员 call（请求带 ghosts）→ 收贡献 → 残差主导收敛
    判定（coarse）/ 子域 delta 聚合（非 coarse，conv 由成员 service 线程
    基于自身上一贡献计算）→ 粗校正 → 下轮；收敛全员 done → 写 sol_t（持久
    ）/iters_t/converged_t → 提交 controller(t)。

    存活不变式（裁定）：任一 call FAILED/断连异常 → alive < nsd → 立即判组
    死 raise。重投语义（裁定）：池缓存 miss → 组散 → 以新 gen 从 setup 重
    新驱动本时间步。"""
    import numpy as np
    import scipy.sparse as sp
    from fly import get_cache, has_cache
    from fly.runtime import get_agent

    agent = get_agent()
    pool_key = f"__rasg__d_pool_{gen}"
    if not has_cache(pool_key):
        new_gen = uuid.uuid4().hex[:8]
        INFO(f"[RASG DYN CHECK] t={t} gen={gen} pool missing — group is "
             f"scattered, restarting the step from setup with gen={new_gen}")
        _submit_setup_chain(db, matrix_ref, nsd, sol_prefix, num_steps,
                            update_rhs, max_iter, tol, omega, min_steps,
                            new_gen, start_t=t)
        return

    pool = get_cache(pool_key)
    lu_key, P_key, A_key, sub_key = _check_data_cache_keys(matrix_ref)
    Ac_lu = get_cache(lu_key)
    P = get_cache(P_key)
    A_fine = get_cache(A_key)
    sub_cache = get_cache(sub_key)
    use_coarse = (omega == "coarse")

    coord = db.read_object("__rasg__coord")
    N = coord["N"]
    primary_sets = coord["primary_sets"]
    ps_arrays = [np.asarray(ps) for ps in primary_sets]

    b_t = np.asarray(db.read_object(f"__rasg__b_{t}"), dtype=np.float64)
    b_norm = max(float(np.linalg.norm(b_t)), 1e-30)

    alive = set(pool.keys())
    try:
        x_global = None
        converged = False
        r_rel = float("inf")
        step = 0
        iters = 0
        while step < max_iter:
            # 存活不变式（裁定）：任一成员失联即组死，当轮入口立即判。
            if len(alive) < nsd:
                raise RuntimeError(
                    f"[RASG DYN CHECK] t={t} group dead: alive="
                    f"{sorted(alive)} of {nsd}")

            contributions = {}
            conv_flags = []
            for sd in sorted(alive):
                sub = sub_cache[sd]
                ghosts = np.zeros(len(sub["outside_coeffs"]))
                if step == 0:
                    if t > 0:
                        ghosts = np.asarray(
                            db.read_object(f"{sol_prefix}_{t - 1}"),
                            dtype=np.float64)[sub["outside_global_idx"]]
                else:
                    ghosts = x_corrected[sub["outside_global_idx"]]
                status, resp = agent.peer_rpc_call(
                    pool[sd], pickle.dumps({
                        "action": "iterate", "step": step,
                        "ghosts": serialize_array(ghosts)}),
                    0)  # timeout_ms=0：无限，断连事件唤醒（timeout 裁定）
                if status != _RPC_OK:
                    alive.discard(sd)
                    raise RuntimeError(
                        f"[RASG DYN CHECK] t={t} rpc sd={sd} status={status}"
                        f" at step={step}")
                data = pickle.loads(resp)
                contributions[sd] = deserialize_array(data["x"])
                conv_flags.append(bool(data.get("conv")))

            x_global = np.zeros(N, dtype=np.float64)
            for sd in contributions:
                x_global[ps_arrays[sd]] = contributions[sd]

            # 收敛判定：coarse 残差主导（数学准则直接界定解误差）；非
            # coarse 无 A_fine，退回子域 delta 标志聚合。
            r_rel = float(np.linalg.norm(b_t - A_fine.dot(x_global))) / b_norm
            if use_coarse:
                converged = step >= min_steps and r_rel < tol
            else:
                converged = step >= min_steps and all(conv_flags)

            if converged or step == max_iter - 1:
                iters = step + 1
                break

            if use_coarse and Ac_lu is not None:
                e_c = Ac_lu.solve(P.T.dot(b_t - A_fine.dot(x_global)))
                x_corrected = x_global + P.dot(e_c)
            else:
                x_corrected = x_global
            step += 1

        INFO(f"[RASG DYN CHECK] t={t} done: converged={converged} "
             f"step={step} r_rel={r_rel:.2e} iters={iters}")

        # 结果落库（写前 remove：重投链换 gen 后 hash 不同，不清会被
        # provenance/DUPLICATE 拦截）。
        for name in [f"{sol_prefix}_{t}", f"__rasg__iters_{t}",
                     f"__rasg__converged_{t}"]:
            _remove_quiet(db, name)
        db.write_object(f"{sol_prefix}_{t}", x_global)
        db.write_object(f"__rasg__iters_{t}", iters, save_to_db=False)
        db.write_object(f"__rasg__converged_{t}", converged, save_to_db=False)

        controller_dyn_task(db, matrix_ref, nsd, sol_prefix, num_steps,
                            update_rhs, max_iter, tol, omega, min_steps,
                            gen, t)
    except Exception:
        # check 侧死亡纪律：强关 server —— 全体成员挂起的 recv_request 被
        # 错误断连唤醒转 raise；清池缓存（重投的 check 据此触发组散重启）。
        try:
            agent.stop_peer_rpc()
        except Exception:
            pass
        try:
            from fly import remove_cache
            remove_cache(pool_key)
        except Exception:
            pass
        raise


# ───────────────────── 阶段 3：controller（链推进 / 收尾清理） ─────────────────────

@as_task(inputs=lambda db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                     max_iter, tol, omega, min_steps, gen, t: [
         db.get_full_name(f"__rasg__converged_{t}")],
         requires=lambda db, matrix_ref, nsd, sol_prefix, num_steps,
                     update_rhs, max_iter, tol, omega, min_steps, gen,
                     t: [db.worker_attr("check")],
         priority=_DYNAMIC_TASK_PRIORITY)
def controller_dyn_task(db, matrix_ref, nsd, sol_prefix, num_steps, update_rhs,
                        max_iter, tol, omega, min_steps, gen, t):
    """时间步控制器（worker_attr("check") 绑定：与 check 同 worker，复用其池缓存）。

    有下一步：update_rhs → 写 b_{t+1} → 提交 solver(t+1) → 删 b_t。
    无下一步（步数尽 / update_rhs 返 None）：本 worker 收尾 → cleanup × nsd
    → 写 dynamic_done（异步善后，不阻塞于 cleanup 完成）。"""
    from fly import remove_cache
    from fly.runtime import get_agent

    x_t = db.read_object(f"{sol_prefix}_{t}")

    if t + 1 < num_steps:
        b_next = update_rhs(x_t, t + 1)
        if b_next is None:
            INFO(f"[RASG DYN CTRL] t={t} update_rhs returned None, stopping early")
            _teardown(db, matrix_ref, nsd, sol_prefix, num_steps, gen, last_t=t)
            return
        _remove_quiet(db, f"__rasg__b_{t + 1}")
        db.write_object(f"__rasg__b_{t + 1}", b_next, save_to_db=False)
        INFO(f"[RASG DYN CTRL] t={t} → t={t + 1} rhs updated")
        _submit_solver_group(db, matrix_ref, nsd, sol_prefix, num_steps,
                             update_rhs, max_iter, tol, omega, min_steps,
                             gen, t + 1)
        _remove_quiet(db, f"__rasg__b_{t}")
        return

    INFO(f"[RASG DYN CTRL] t={t} final step, tearing down")
    _teardown(db, matrix_ref, nsd, sol_prefix, num_steps, gen, last_t=t)


def _teardown(db, matrix_ref, nsd, sol_prefix, num_steps, gen, last_t):
    """收尾：本 worker（check 侧）清理 → cleanup × nsd → dynamic_done。"""
    from fly import remove_cache, has_cache
    from fly.runtime import get_agent

    def _remove_cache_quiet(key):
        # 清理幂等（2026-08-31 修复）：非 coarse 模式 lu/P key 从未 put，
        # agent.remove_cache 的 KeyError 语义会把 controller 炸掉 →
        # dynamic_done 永不产生（wait_obj 报 cannot be produced）。
        if has_cache(key):
            remove_cache(key)

    agent = get_agent()
    agent.stop_peer_rpc()          # 关池连接（成员均已收到 done 正常退出）
    _remove_cache_quiet(f"__rasg__d_pool_{gen}")
    agent.remove_worker_property(db.worker_attr(f"{gen}_check"))
    for key in _check_data_cache_keys(matrix_ref):
        _remove_cache_quiet(key)

    for sd in range(nsd):
        cleanup_task(db, matrix_ref, sd, gen, last_t)

    iters = [db.read_object(f"__rasg__iters_{i}") for i in range(last_t + 1)]
    conv = [db.read_object(f"__rasg__converged_{i}") for i in range(last_t + 1)]
    db.write_object("__rasg__dynamic_done", {
        "num_steps_done": last_t + 1,
        "iters": iters,
        "converged": conv,
        "sol_names": [f"{sol_prefix}_{i}" for i in range(last_t + 1)],
    }, save_to_db=False)
    # 链尾产出 __rasg__sol（最后一步全局解，持久化对象）：它是 flow（Solver
    # Project）的对外契约对象——case 在 wait_frozen 之后仍要 read_object。
    # temp（save_to_db=False）读写行为与持久化一致，差异仅在生命周期：
    # db freeze 时自动删除（iters/converged 等内部中间量用 temp 属预期清
    # 理）。故 freeze 后仍需可读的契约对象必须持久化。kickoff 在 worker 上
    # 非阻塞提交后无人等待，结果统一由链尾落地；golden（master 阻塞调
    # solve_once）同路径幂等（同名同值）。
    db.write_object("__rasg__sol", db.read_object(f"{sol_prefix}_{last_t}"))
    INFO(f"[RASG DYN CTRL] dynamic done: steps={last_t + 1} iters={iters}")


@as_task(inputs=lambda db, matrix_ref, sd, gen, final_t: [
             db.get_full_name(f"__rasg__converged_{final_t}")],
         requires=lambda db, matrix_ref, sd, gen, final_t: [db.worker_attr(f"sd_{sd}")],
         priority=_DYNAMIC_TASK_PRIORITY)
def cleanup_task(db, matrix_ref, sd, gen, final_t):
    """compute worker 收尾：销毁矩阵/listener 缓存、关 server、移除属性、
    删地址对象。requires=[worker_attr("sd_{sd}")] 单线程队列天然排在最后一步
    compute 之后；inputs=[converged_final_t] 双保险锚定全局完成。"""
    from fly import remove_cache
    from fly.runtime import get_agent

    from fly import get_cache as _gc, has_cache as _hc
    if _hc(f"__rasg__d_svc_{gen}_{sd}"):
        _gc(f"__rasg__d_svc_{gen}_{sd}")["stop"] = True   # 服务线程退出开关

    agent = get_agent()
    agent.stop_peer_rpc()   # 兜底：阻塞在 recv 的服务线程被断连唤醒退出
    setup_key, solver_key = _data_cache_keys(matrix_ref, sd)
    # 清理幂等（同 _teardown 2026-08-31 修复：remove_cache 的 KeyError 语义）。
    from fly import has_cache as _has
    for _k in (setup_key, solver_key, f"__rasg__d_svc_{gen}_{sd}"):
        if _has(_k):
            remove_cache(_k)
    agent.remove_worker_property(db.worker_attr(f"{gen}_{sd}"))
    _remove_quiet(db, f"__rasg__d_addr_{gen}_{sd}")
    INFO(f"[RASG DYN CLEANUP] sd={sd} gen={gen} done")


# ───────────────── 公共矩阵工具（2026-08-31 自 ras_graph.py 搬入，v1 退役）─────────────────

MATRIX_OBJ_KEY = "__rasg__matrix"


def _load_matrix(ref, db=None):
    """Load matrix data. Returns dict with n, N, rows, cols, vals, b, x_exact.

    双模式：db 非空 → ref 为 DB 对象名（read_object，矩阵作为分布式数据由
    框架管理）；db 为空 → ref 为 .npz 文件路径（本地实验脚本用）。

    Values stay as numpy arrays (no .tolist()) — downstream consumers (the
    ex_slv_* C++ helpers and scipy sparse constructors) accept arrays directly,
    and nanobind converts a contiguous int64/float64 array to std::vector far
    faster than iterating a Python list.
    """
    import numpy as np
    if db is not None:
        data = db.read_object(ref)
        result = {
            "n": int(data["n"]),
            "N": int(data["N"]),
            "rows": data["rows"],
            "cols": data["cols"],
            "vals": data["vals"],
            "b": data["b"],
        }
        if "x_exact" in data:
            result["x_exact"] = data["x_exact"]
        return result
    data = np.load(ref, allow_pickle=False)
    result = {
        "n": int(data["n"]),
        "N": int(data["N"]),
        "rows": data["rows"],
        "cols": data["cols"],
        "vals": data["vals"],
        "b": data["b"],
    }
    if "x_exact" in data.files:
        result["x_exact"] = data["x_exact"]
    return result


def _get_matrix_data(ref, db=None):
    """Load matrix data with process-local caching（双模式，见 _load_matrix）."""
    from fly import get_cache, has_cache, put_cache
    cache_key = f"__rasg__matrix_{ref}"
    if not has_cache(cache_key):
        data = _load_matrix(ref, db)
        put_cache(cache_key, data)
    return get_cache(cache_key)


def generate_poisson_matrix(n, path, compute_exact=True):
    """Generate a Poisson 2D matrix and save to .npz file.

    Creates a 5-point stencil Laplacian on an n×n grid.
    RHS b = [1.0] * N. Golden solution computed via scipy.sparse.linalg.splu.
    """
    import os
    import numpy as np
    from scipy import sparse
    from scipy.sparse.linalg import splu

    N = n * n
    diag_idx = np.arange(N)
    mask_r = (diag_idx + 1) % n != 0
    mask_l = diag_idx % n != 0
    r_right = diag_idx[mask_r]
    r_left = diag_idx[mask_l]
    r_down = diag_idx[:N - n]
    r_up = diag_idx[n:]
    all_rows = np.concatenate([diag_idx, r_right, r_left, r_down, r_up])
    all_cols = np.concatenate([diag_idx, r_right + 1, r_left - 1,
                               r_down + n, r_up - n])
    all_vals = np.concatenate([
        np.full(N, 4.0),
        np.full(len(r_right), -1.0), np.full(len(r_left), -1.0),
        np.full(len(r_down), -1.0), np.full(len(r_up), -1.0),
    ])
    A_csc = sparse.csc_matrix((all_vals, (all_rows, all_cols)), shape=(N, N))

    col_idx = np.repeat(np.arange(N), np.diff(A_csc.indptr))
    rows = A_csc.indices.astype(np.int64)
    cols = col_idx.astype(np.int64)
    vals = A_csc.data.astype(np.float64)

    b = np.ones(N, dtype=np.float64)
    tmp_path = path + ".tmp_gen"
    with open(tmp_path, "wb") as f:
        np.savez(f, n=np.int64(n), N=np.int64(N),
                 rows=rows, cols=cols, vals=vals, b=b)
    os.replace(tmp_path, path)
    if compute_exact:
        x_exact = splu(A_csc).solve(b)
        tmp_path = path + ".tmp_exact"
        with open(tmp_path, "wb") as f:
            np.savez(f, n=np.int64(n), N=np.int64(N),
                     rows=rows, cols=cols, vals=vals, b=b, x_exact=x_exact)
        os.replace(tmp_path, path)
    INFO(f"[MATRIX] Generated n={n} N={N} nnz={len(vals)} path={path}")


# ───────────────── 单次求解便捷入口（求解器收敛 2026-08-31：单次=多时间步单步）─────────────────

def solve_once(db, matrix_ref, nsd, *, overlap_ratio=0.50, max_iter=100,
               tol=1e-8, omega=1.0, b=None, sol_prefix="__rasg__sol"):
    """单次求解（dynamic 单步封装，阻塞至完成）。

    返回 {"x", "iters", "converged"}（与退役的 solve_ras_graph 返回结构
    一致，迁移方零适配）。b=None 时取矩阵自带 rhs。
    """
    if b is None:
        b = _get_matrix_data(matrix_ref, db)["b"]
    solve_ras_graph_dynamic(
        db, matrix_ref, nsd, b, lambda x, t: None,
        num_steps=1, overlap_ratio=overlap_ratio, max_iter=max_iter,
        tol=tol, omega=omega, sol_prefix=sol_prefix)
    res = get_dynamic_result(db)
    last = res["num_steps_done"] - 1
    return {
        "x": db.read_object(f"{sol_prefix}_{last}"),
        "iters": res["iters"][last],
        "converged": res["converged"][last],
    }


# ───────────── 几何/分块/coarse 预构建（2026-08-31 自 ras_graph(v1)/ras_graph_daemon(v2) 搬入）─────────────
# v1/v2 退役（用户裁定：单次=多时间步单步，仅保留 dynamic），其公共函数族
# 全部收拢本文件——dynamic 成为唯一求解实现（间接依赖链 kickoff→v2 pipeline
# →v1 函数族曾长期存在，本次收敛显式化）。

import math


def _factor_nsd(nsd):
    best_x, best_y = 1, nsd
    best_diff = nsd
    for x in range(1, int(math.sqrt(nsd)) + 1):
        if nsd % x == 0:
            y = nsd // x
            diff = abs(y - x)
            if diff < best_diff:
                best_x, best_y = x, y
                best_diff = diff
    return best_x, best_y


def _distribute(n, parts):
    bounds = [0]
    base = n // parts
    extra = n % parts
    pos = 0
    for i in range(parts):
        pos += base + (1 if i < extra else 0)
        bounds.append(pos)
    return bounds


def _partition_primary_2d(n, nsd):
    nsd_x, nsd_y = _factor_nsd(nsd)
    row_bounds = _distribute(n, nsd_x)
    col_bounds = _distribute(n, nsd_y)
    primary_sets = []
    for ix in range(nsd_x):
        for iy in range(nsd_y):
            nodes = []
            for r in range(row_bounds[ix], row_bounds[ix + 1]):
                for c in range(col_bounds[iy], col_bounds[iy + 1]):
                    nodes.append(r * n + c)
            primary_sets.append(nodes)
    return primary_sets, nsd_x, nsd_y


def _estimate_depth(n, nsd_x, nsd_y, overlap_ratio):
    L = min(n // nsd_x, n // nsd_y)
    return max(2, math.ceil(overlap_ratio * L / 2))


def _compute_grid_neighbors(nsd_x, nsd_y):
    """Compute neighbor subdomain IDs from 2D grid layout (grid adjacency)."""
    neighbors = []
    for ix in range(nsd_x):
        for iy in range(nsd_y):
            nb = []
            for dix, diy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
                nix, niy = ix + dix, iy + diy
                if 0 <= nix < nsd_x and 0 <= niy < nsd_y:
                    nb.append(nix * nsd_y + niy)
            neighbors.append(nb)
    return neighbors


def _compute_coarse_arrays(n, N, rows, cols, vals):
    """Shared bilinear interpolation + Galerkin projection for the coarse grid.

    Builds the restriction operator P (fine → coarse via bilinear interpolation)
    and the Galerkin coarse operator Ac = P^T A_fine P. Returns None when the
    coarse grid is too small to be useful.
    """
    from scipy import sparse

    stride = max(2, n // 125)
    nc = n // stride
    Nc = nc * nc
    if Nc < 10:
        return None

    fi_arr, fj_arr = np.divmod(np.arange(N), n)
    ci_f = fi_arr / stride
    cj_f = fj_arr / stride
    ci0 = np.minimum(ci_f.astype(np.int64), nc - 1)
    cj0 = np.minimum(cj_f.astype(np.int64), nc - 1)
    di = ci_f - ci0
    dj = cj_f - cj0
    ci1 = np.minimum(ci0 + 1, nc - 1)
    cj1 = np.minimum(cj0 + 1, nc - 1)
    fine_idx = np.arange(N)
    pr, pc, pv = [], [], []
    for ci, cj, w in [
        (ci0, cj0, (1 - di) * (1 - dj)),
        (ci0, cj1, (1 - di) * dj),
        (ci1, cj0, di * (1 - dj)),
        (ci1, cj1, di * dj),
    ]:
        mask = w > 1e-15
        pr.append(fine_idx[mask])
        pc.append((ci * nc + cj)[mask])
        pv.append(w[mask])
    P_rows = np.concatenate(pr)
    P_cols = np.concatenate(pc)
    P_vals = np.concatenate(pv)
    P = sparse.csr_matrix((P_vals, (P_rows, P_cols)), shape=(N, Nc))
    A_fine = sparse.csr_matrix((vals, (rows, cols)), shape=(N, N))
    Ac = (P.T @ (A_fine @ P)).tocsc()
    return P, P_rows, P_cols, P_vals, A_fine, Ac, stride, nc, Nc


def _build_coarse_operators(n, N, matrix_ref, db=None):
    """Build global coarse-grid P (restriction) and Galerkin Ac on the coord
    process. Returns serialisable raw sparse data (COO for P, CSC for Ac) so
    workers can rebuild the sparse objects and do only the LU step.
    Returns None if the coarse grid is too small to be useful.
    """
    data = _get_matrix_data(matrix_ref, db)
    rows = np.asarray(data["rows"])
    cols = np.asarray(data["cols"])
    vals = np.asarray(data["vals"])

    result = _compute_coarse_arrays(n, N, rows, cols, vals)
    if result is None:
        return None
    P, P_rows, P_cols, P_vals, A_fine, Ac, stride, _nc, Nc = result

    return {
        "P_rows": P_rows, "P_cols": P_cols, "P_vals": P_vals,
        "N": N, "Nc": int(Nc),
        "Ac_indptr": Ac.indptr.astype(np.int64),
        "Ac_indices": Ac.indices.astype(np.int64),
        "Ac_data": Ac.data.astype(np.float64),
        "Ac_shape": (int(Ac.shape[0]), int(Ac.shape[1])),
        "b": np.asarray(data["b"], dtype=np.float64),
        "stride": stride,
    }


def _prebuild_coarse_in_coord(db, n, N, matrix_ref):
    """Coord-side: build coarse operators once, publish raw data to DB."""
    t0 = time.perf_counter()
    result = _build_coarse_operators(n, N, matrix_ref, db)
    if result is None:
        INFO("[RASG COARSE] Skipping: coarse grid too small")
        db.write_object("__rasg__coarse_prebuilt", {"skip": True}, save_to_db=False)
        return
    db.write_object("__rasg__coarse_prebuilt", result, save_to_db=False)
    INFO(f"[RASG COARSE] coord prebuild: {(time.perf_counter()-t0)*1000:.0f}ms "
         f"Nc={result['Nc']}")


def _coord_prebuild_pipeline(db, matrix_ref, nsd, overlap_ratio, max_iter, tol,
                             omega, group_id=None, on_sub_ready=None):
    """coord 预构建 + 流水线提交：每完成一个 sub_{sd} 调 on_sub_ready（可选）。

    自 ras_graph_daemon(v2) 搬入（2026-08-31 收敛）：dynamic 模式
    on_sub_ready=None（task 组由 controller 按时间步提交）；单步路径
    （solve_once）经 kickoff_dyn_task 同样 None。v2 的 _prebuild_coarse_grid
    （per-worker LU 预分发）不搬——dynamic 的 LU 由 setup_check 按 matrix_ref
    key 缓存自建。"""
    import scipy.sparse as sp

    data = _load_matrix(matrix_ref, db)
    n = data["n"]; N = data["N"]
    rows, cols, vals = data["rows"], data["cols"], data["vals"]
    b = data["b"]

    primary_sets, nsd_x, nsd_y = _partition_primary_2d(n, nsd)
    if overlap_ratio <= 0: overlap_ratio = 0.50
    depth = _estimate_depth(n, nsd_x, nsd_y, overlap_ratio)

    global_owner = {}
    for sd_id in range(nsd):
        for gidx in primary_sets[sd_id]:
            global_owner[gidx] = sd_id

    neighbor_ids_all = _compute_grid_neighbors(nsd_x, nsd_y)

    coord = {
        "nsd": nsd, "N": N, "n": n, "nsd_x": nsd_x, "nsd_y": nsd_y,
        "overlap_ratio": overlap_ratio, "depth": depth,
        "primary_sets": primary_sets, "global_owner": global_owner,
        "matrix_ref": matrix_ref,
    }
    db.write_object("__rasg__coord", coord, save_to_db=False)
    cfg = {
        "nsd": nsd, "N": N, "n": n, "max_iter": max_iter, "tol": tol,
        "omega": omega, "primary_sets": primary_sets,
        "neighbor_ids_all": neighbor_ids_all, "matrix_ref": matrix_ref,
    }
    db.write_object("__rasg__cfg", cfg, save_to_db=False)

    INFO(f"[RASG COORD] n={n} nsd={nsd} ({nsd_x}x{nsd_y}) depth={depth}")

    rows_arr = np.asarray(rows); cols_arr = np.asarray(cols); vals_arr = np.asarray(vals)
    A_bool = sp.csr_matrix((np.ones(len(rows_arr)), (rows_arr, cols_arr)), shape=(N, N))

    def _bfs(seed_indices, depth):
        mask = np.zeros(N, dtype=np.float64)
        mask[seed_indices] = 1.0
        expanded = np.zeros(N, dtype=bool)
        expanded[seed_indices] = True
        for _ in range(depth):
            neighbors = A_bool.dot(mask)
            new = (neighbors > 0) & (~expanded)
            if not new.any(): break
            expanded |= new
            mask[new] = 1.0
        return np.where(expanded)[0]

    for sd in range(nsd):
        primary_nodes = np.asarray(primary_sets[sd])
        local_idx = _bfs(primary_nodes, depth)
        ratio = len(local_idx) / len(primary_nodes)
        if ratio < 1 + overlap_ratio:
            local_idx = _bfs(primary_nodes, depth * 2)

        local_idx_map = {g: p for p, g in enumerate(local_idx)}
        primary_local_pos = np.array([local_idx_map[g] for g in primary_nodes], dtype=np.int64)

        _rank = np.full(N, -1, dtype=np.int32)
        _rank[local_idx] = np.arange(len(local_idx))
        row_rank = _rank[rows_arr]; col_rank = _rank[cols_arr]
        in_local = (row_rank >= 0) & (col_rank >= 0)
        a_rows = row_rank[in_local]; a_cols = col_rank[in_local]; a_vals = vals_arr[in_local]
        is_outside = ((col_rank >= 0) & (row_rank < 0) & (rows_arr != cols_arr) & (vals_arr != 0.0))
        out_pos = col_rank[is_outside]; out_gidx = rows_arr[is_outside]; out_coeffs = vals_arr[is_outside]
        b_local = b[local_idx]

        neighbor_needed = {}
        for i, g in enumerate(out_gidx):
            owner = global_owner.get(int(g), -1)
            if owner >= 0 and owner != sd:
                neighbor_needed.setdefault(owner, []).append(i)
        actual_neighbor_ids = sorted(neighbor_needed.keys())

        neighbor_recv_idx = {}
        for nb_id in actual_neighbor_ids:
            nb_pm = {g: p for p, g in enumerate(primary_sets[nb_id])}
            recv_positions = []; need_global = []
            for ci in neighbor_needed[nb_id]:
                og = int(out_gidx[ci])
                recv_positions.append(nb_pm.get(og, -1))
                need_global.append(og)
            neighbor_recv_idx[nb_id] = recv_positions

        subdomain_data = {
            "sd_id": sd,
            "local_indices": np.array(local_idx, dtype=np.int64),
            "primary_local_pos": primary_local_pos,
            "b_orig": np.array(b_local, dtype=np.float64),
            "outside_local_pos": np.array(out_pos, dtype=np.int64),
            "outside_global_idx": np.array(out_gidx, dtype=np.int64),
            "outside_coeffs": np.array(out_coeffs, dtype=np.float64),
            "neighbor_ids": actual_neighbor_ids,
            "neighbor_recv_idx": neighbor_recv_idx,
            "neighbor_needed": neighbor_needed,
            "a_rows": np.array(a_rows, dtype=np.int64),
            "a_cols": np.array(a_cols, dtype=np.int64),
            "a_vals": np.array(a_vals, dtype=np.float64),
            "size": len(local_idx),
        }
        db.write_object(f"__rasg__sub_{sd}", subdomain_data, save_to_db=False)
        INFO(f"[RASG COORD] subdomain {sd}: primary={len(primary_nodes)} "
             f"extended={len(local_idx)} ratio={ratio:.2f}x neighbors={actual_neighbor_ids}")

        if on_sub_ready is not None:
            on_sub_ready(sd)

    if omega == "coarse":
        _prebuild_coarse_in_coord(db, n, N, matrix_ref)


def compute_exact_from_matrix(data):
    """内存版精确解：从矩阵 dict 直接 splu 求解，返回 x_exact（无文件 IO）。

    golden 验证链使用。自 ras_graph.py(v1) 搬入（2026-08-31 收敛）。
    """
    from scipy import sparse
    from scipy.sparse.linalg import splu
    N = int(data["N"])
    A_csc = sparse.csc_matrix(
        (data["vals"], (data["rows"], data["cols"])), shape=(N, N))
    return splu(A_csc).solve(data["b"])


def compute_exact_solution(n, path):
    """Compute x_exact via splu and append it to an existing matrix .npz.

    Idempotent: re-saves the file with x_exact added. 原子写（tmp+replace，
    并行读写方永无截断中间态——P3-24）。
    """
    import os
    import numpy as np
    from scipy import sparse
    from scipy.sparse.linalg import splu
    data = np.load(path, allow_pickle=False)
    N = int(data["N"])
    A_csc = sparse.csc_matrix(
        (data["vals"], (data["rows"], data["cols"])), shape=(N, N))
    x_exact = splu(A_csc).solve(data["b"])
    tmp_path = path + ".tmp_exact"
    with open(tmp_path, "wb") as f:
        np.savez(f, n=data["n"], N=data["N"],
                 rows=data["rows"], cols=data["cols"], vals=data["vals"],
                 b=data["b"], x_exact=x_exact)
    os.replace(tmp_path, path)
    return x_exact
