"""RAS solver 迭代重构版（常驻 task + PeerChannelGroup RPC 直连）。

vs ras_graph.py（task 链驱动）：
  - nsd+1 个常驻 while task（nsd compute + 1 check），RPC 直连迭代
  - 消除每轮 task 调度开销（22%）+ DB 数据交换
  - 失败传播：RPC 超时/断连/notify_failure → 各自退出

设计见 docs/solver/iter-refactor-design.md + iter-refactor-impl-plan.md。
"""
import time
import pickle
import numpy as np
from _fly_log import DBG, INFO, WARN, ERR
from fly import as_task, wait_obj
from agent import PeerChannelGroup, serialize_array, deserialize_array


def _coord_prebuild(db, matrix_path, nsd, overlap_ratio, max_iter, tol, omega):
    """coord 预构建（写 coord/cfg/coarse），不提交 compute/check task。

    从 ras_graph_coord 抽取，去掉末尾的 task 提交（setup/compute/check）。
    setup/compute/check 由 daemon task 接管。
    """
    from solver.ras_graph import (_load_matrix, _partition_primary_2d,
                                   _estimate_depth, _compute_grid_neighbors,
                                   _prebuild_coarse_in_coord, _prebuild_coarse_grid)

    data = _load_matrix(matrix_path)
    n = data["n"]
    N = data["N"]

    primary_sets, nsd_x, nsd_y = _partition_primary_2d(n, nsd)
    if overlap_ratio <= 0:
        overlap_ratio = 0.50
    depth = _estimate_depth(n, nsd_x, nsd_y, overlap_ratio)

    global_owner = {}
    for sd_id in range(nsd):
        for gidx in primary_sets[sd_id]:
            global_owner[gidx] = sd_id

    neighbor_ids_all = _compute_grid_neighbors(nsd_x, nsd_y)

    coord = {
        "nsd": nsd, "N": N, "n": n,
        "nsd_x": nsd_x, "nsd_y": nsd_y,
        "overlap_ratio": overlap_ratio,
        "depth": depth,
        "primary_sets": primary_sets,
        "global_owner": global_owner,
        "matrix_path": matrix_path,
    }
    db.write_object("__rasg__coord", coord, save_to_db=False)
    cfg = {
        "nsd": nsd, "N": N, "n": n,
        "max_iter": max_iter, "tol": tol,
        "omega": omega,
        "primary_sets": primary_sets,
        "neighbor_ids_all": neighbor_ids_all,
        "matrix_path": matrix_path,
    }
    db.write_object("__rasg__cfg", cfg, save_to_db=False)

    INFO(f"[RASG V2 COORD] n={n} nsd={nsd} ({nsd_x}x{nsd_y}) depth={depth}")

    if omega == "coarse":
        _prebuild_coarse_in_coord(db, n, N, matrix_path)
        _prebuild_coarse_grid(db, nsd)


def solve_ras_graph_v2(db, matrix_path, nsd,
                       overlap_ratio=0.50, max_iter=100, tol=1e-8,
                       omega=1.0, max_concurrent_compute=None):
    """Solve using 常驻 daemon task + PeerChannelGroup RPC 直连。

    nsd+1 个 worker：nsd 个 compute（带 sd_X 属性）+ 1 个 check（带 check 属性）。
    """
    from fly.runtime import get_agent

    n_workers = nsd + 1
    master = get_agent()
    if not master.is_running() or master.worker_count < n_workers:
        worker_configs = [{"attributes": ["check"]}]  # check worker
        for w in range(nsd):
            worker_configs.append({"attributes": [f"sd_{w}"]})
        master.launch_local_workers(worker_configs)
        assert master.wait_for_workers(n_workers), f"{n_workers} workers should connect"

    INFO(f"[RASG V2] nsd={nsd} n_workers={n_workers}")

    # coord 预构建（只写 coord/cfg/coarse，不提交 compute/check task）
    _coord_prebuild(db, matrix_path, nsd, overlap_ratio, max_iter, tol, omega)

    # 创建 PeerChannelGroup
    group = PeerChannelGroup()
    INFO(f"[RASG V2] group_id={group.group_id[:8]}")

    # 提交 check daemon + nsd 个 compute daemon
    check_daemon_task(db, group.group_id, nsd, max_iter, tol, omega)
    for sd in range(nsd):
        compute_daemon_task(db, group.group_id, sd, nsd, omega)

    return get_ras_graph_solution_v2(db)


@as_task(requires=lambda db, group_id, sd, nsd, omega:
         [f"sd_{sd}"])
def compute_daemon_task(db, group_id, sd, nsd, omega_strategy):
    """常驻 compute task：setup → connect check → while 循环 solve + RPC。"""
    from _fly_solver import ex_slv_ras_bupdated_solve
    from fly import get_cache, put_cache, has_cache
    from solver.ras_graph import ras_graph_setup, _compute_grid_neighbors, _factor_nsd

    # ── Setup（复用 ras_graph_setup）──
    nsd_x, nsd_y = _factor_nsd(nsd)
    neighbor_ids = _compute_grid_neighbors(nsd_x, nsd_y)[sd]
    ras_graph_setup(db, sd, nsd, neighbor_ids)
    setup = get_cache(f"__rasg__setup_{sd}")
    solver = get_cache(f"__rasg__solver_{sd}")

    cfg = db.read_object("__rasg__cfg")
    tol = cfg["tol"]

    # ── Connect check ──
    group = PeerChannelGroup(group_id)
    chan = group.connect(db, timeout=60)
    INFO(f"[COMPUTE DAEMON sd={sd}] connected to check")

    # ── 迭代循环 ──
    step = 0
    use_coarse = (omega_strategy == "coarse")
    while True:
        # 读邻居解
        outside_coeffs = setup["outside_coeffs"]
        neighbor_values = [0.0] * len(outside_coeffs)
        if step > 0:
            x_prefix = "__rasg__xc_" if use_coarse else "__rasg__x_"
            for nb_id in setup["neighbor_ids"]:
                nb_key = f"__nb_x_{nb_id}_{step-1}"
                nb_x = get_cache(nb_key) if has_cache(nb_key) else db.read_object(f"{x_prefix}{nb_id}_{step-1}")
                put_cache(nb_key, nb_x)
                recv_positions = setup["neighbor_recv_idx"][nb_id]
                conn_indices = setup["neighbor_needed"][nb_id]
                for i, conn_i in enumerate(conn_indices):
                    pos = recv_positions[i]
                    if pos >= 0:
                        neighbor_values[conn_i] = nb_x[pos]

        # 本地 solve
        x_local = ex_slv_ras_bupdated_solve(
            solver, setup["b_orig"],
            setup["outside_local_pos"], outside_coeffs,
            neighbor_values, 1.0)
        x_primary = np.asarray(x_local, dtype=np.float64)[setup["primary_local_pos"]]

        # 收敛检查（本地）
        converged_local = False
        prev_x_key = f"__rasg__prev_x_{sd}"
        if step > 0 and has_cache(prev_x_key):
            prev_x = np.asarray(get_cache(prev_x_key), dtype=np.float64)
            max_delta = float(np.max(np.abs(x_primary - prev_x)))
            converged_local = max_delta < tol
        put_cache(prev_x_key, x_primary)

        # RPC 发本轮解给 check（payload 带 sd + conv + x_primary）
        payload = pickle.dumps({
            "sd": sd, "step": step, "conv": converged_local,
            "x": serialize_array(x_primary),
        })
        try:
            status, resp = chan.rpc(payload, timeout=30)
        except Exception as e:
            INFO(f"[COMPUTE DAEMON sd={sd}] RPC failed at step={step}: {e}")
            break
        if status != 0:
            INFO(f"[COMPUTE DAEMON sd={sd}] check reported failure at step={step}")
            break

        result = pickle.loads(resp)
        if result["action"] == "done":
            INFO(f"[COMPUTE DAEMON sd={sd}] done at step={step}")
            break
        # continue：应用粗校正（xc 是 check 校正后的本子域解）
        if "xc" in result:
            xc = deserialize_array(result["xc"])
            put_cache(prev_x_key, xc)
        step += 1

    chan.close()
    INFO(f"[COMPUTE DAEMON sd={sd}] exited at step={step}")


@as_task(requires=lambda db, group_id, nsd, max_iter, tol, omega:
         ["check"])
def check_daemon_task(db, group_id, nsd, max_iter, tol, omega_strategy):
    """常驻 check task：listen → accept → 收齐 nsd 份 → 粗校正 → 回复 → 循环。"""
    group = PeerChannelGroup(group_id)
    listener = group.listen(db)
    INFO(f"[CHECK DAEMON] listening port={listener.port}")

    coord = db.read_object("__rasg__coord")
    N = coord["N"]
    primary_sets = coord["primary_sets"]
    use_coarse = (omega_strategy == "coarse")

    # 粗校正缓存（lazy）
    coarse_ready = [False]  # 用 list 包 mutable

    step = 0
    while step < max_iter:
        # ── 收齐 nsd 份本轮解 ──
        # 每份记录 (conn_id, rpc_id, sd, x_primary, conv)
        contributions = {}
        failed = False
        for _ in range(nsd):
            try:
                conn_id, rpc_id, src, payload = listener.accept_one(timeout=60)
            except Exception:
                failed = True
                break
            if rpc_id == 0:
                WARN(f"[CHECK DAEMON] accept timeout at step={step}")
                failed = True
                break
            data = pickle.loads(payload)
            sd = data["sd"]
            x_primary = deserialize_array(data["x"])
            contributions[sd] = {
                "conn_id": conn_id, "rpc_id": rpc_id,
                "x": x_primary, "conv": data["conv"],
            }

        if failed or len(contributions) < nsd:
            ERR(f"[CHECK DAEMON] only {len(contributions)}/{nsd} at step={step}, abort")
            for sd, c in contributions.items():
                listener.notify_failure(c["conn_id"], "peer timeout")
            break

        # ── 收敛判定 ──
        all_converged = all(c["conv"] for c in contributions.values())

        if all_converged or step == max_iter - 1:
            # assemble 全局解 + 通知 done
            x_global = np.zeros(N, dtype=np.float64)
            for sd in range(nsd):
                x_global[np.asarray(primary_sets[sd])] = contributions[sd]["x"]
            db.write_object("__rasg__sol", x_global)
            db.write_object("__rasg__iters", step + 1)
            db.write_object("__rasg__converged", all_converged)
            for sd in range(nsd):
                c = contributions[sd]
                listener.respond(c["conn_id"], c["rpc_id"],
                                 pickle.dumps({"action": "done"}))
            INFO(f"[CHECK DAEMON] converged={all_converged} at step={step}")
            break

        # ── 粗校正 ──
        # assemble 全局解
        x_global = np.zeros(N, dtype=np.float64)
        for sd in range(nsd):
            x_global[np.asarray(primary_sets[sd])] = contributions[sd]["x"]

        if use_coarse:
            # 写各子域解到 DB（_apply_coarse_correction 从 DB 读）
            for sd in range(nsd):
                db.write_object(f"__rasg__x_{sd}_{step}",
                                contributions[sd]["x"], save_to_db=False)
            # 粗校正（复用 ras_graph 的 _apply_coarse_correction）
            from solver.ras_graph import _apply_coarse_correction
            _apply_coarse_correction(db, step, nsd)
            # 读校正后的解
            xc_dict = {}
            for sd in range(nsd):
                xc_dict[sd] = db.read_object(f"__rasg__xc_{sd}_{step}")
        else:
            xc_dict = {sd: contributions[sd]["x"] for sd in range(nsd)}

        # ── 回复各 compute（continue + 校正解）──
        for sd in range(nsd):
            c = contributions[sd]
            listener.respond(c["conn_id"], c["rpc_id"],
                             pickle.dumps({"action": "continue",
                                           "xc": serialize_array(xc_dict[sd])}))
        step += 1

    listener.close()
    INFO(f"[CHECK DAEMON] exited at step={step}")


@wait_obj(inputs=lambda db: [db.get_full_name("__rasg__sol")])
def get_ras_graph_solution_v2(db, timeout=3600):
    return {
        "x": db.read_object("__rasg__sol"),
        "iters": db.read_object("__rasg__iters"),
        "converged": db.read_object("__rasg__converged"),
    }
