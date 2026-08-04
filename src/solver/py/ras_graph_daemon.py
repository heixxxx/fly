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


def _do_setup(db, sd, nsd):
    """内联 setup（从 ras_graph_setup 抽取核心逻辑，不经 @as_task 装饰器）。

    返回 (setup_dict, solver)。结果也 put_cache 供复用。
    """
    from fly import get_cache, put_cache, has_cache
    from solver.ras_graph import (_get_matrix_data, _factor_nsd,
                                   _compute_grid_neighbors)
    from _fly_solver import EXSlvSubdomainSolver
    import numpy as np

    setup_key = f"__rasg__setup_{sd}"
    solver_key = f"__rasg__solver_{sd}"
    if has_cache(setup_key) and has_cache(solver_key):
        return get_cache(setup_key), get_cache(solver_key)

    coord = db.read_object("__rasg__coord")
    matrix_path = coord["matrix_path"]
    N = coord["N"]
    depth = coord["depth"]
    overlap_ratio = coord["overlap_ratio"]
    primary_nodes = coord["primary_sets"][sd]
    global_owner = coord["global_owner"]
    all_primary_sets = coord["primary_sets"]

    data = _get_matrix_data(matrix_path)
    rows, cols, vals = data["rows"], data["cols"], data["vals"]
    b = data["b"]
    primary_size = len(primary_nodes)

    # BFS overlap expansion（与 ras_graph_setup 一致）
    adj_key = f"__rasg__adj_{matrix_path}"
    if not has_cache(adj_key):
        _ra = np.asarray(rows); _ca = np.asarray(cols)
        _si = np.argsort(_ca, kind="stable")
        put_cache(adj_key, {
            "starts": np.searchsorted(_ca[_si], np.arange(N + 1)),
            "rows": _ra[_si],
        })
    _adj = get_cache(adj_key)

    def _bfs(seed, layers):
        expanded = set(seed); current = list(seed)
        for _ in range(layers):
            frontier = set()
            for node in current:
                s, e = _adj["starts"][node], _adj["starts"][node + 1]
                for row in _adj["rows"][s:e]:
                    if row != node and row not in expanded:
                        frontier.add(int(row))
            if not frontier: break
            expanded |= frontier; current = frontier
        return sorted(expanded)

    local_idx = _bfs(primary_nodes, depth)
    ratio = len(local_idx) / primary_size
    if ratio < 1 + overlap_ratio:
        local_idx = _bfs(primary_nodes, depth * 2)
        ratio = len(local_idx) / primary_size

    local_idx_map = {g: p for p, g in enumerate(local_idx)}
    primary_local_pos = [local_idx_map[g] for g in primary_nodes]

    # rank filter
    rows_arr = np.asarray(rows); cols_arr = np.asarray(cols); vals_arr = np.asarray(vals)
    _rank = np.full(N, -1, dtype=np.int32)
    _rank[local_idx] = np.arange(len(local_idx))
    row_rank = _rank[rows_arr]; col_rank = _rank[cols_arr]
    in_local = (row_rank >= 0) & (col_rank >= 0)
    a_rows = row_rank[in_local]; a_cols = col_rank[in_local]; a_vals = vals_arr[in_local]
    size = len(local_idx)
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
    need_map = {}
    for nb_id in actual_neighbor_ids:
        nb_pm = {g: p for p, g in enumerate(all_primary_sets[nb_id])}
        recv_positions = []; need_global = []
        for ci in neighbor_needed[nb_id]:
            og = int(out_gidx[ci])
            recv_positions.append(nb_pm.get(og, -1))
            need_global.append(og)
        neighbor_recv_idx[nb_id] = recv_positions
        need_map[nb_id] = np.array(need_global, dtype=np.int64)

    setup_data = {
        "sd_id": sd,
        "local_indices": np.array(local_idx, dtype=np.int64),
        "primary_local_pos": np.array(primary_local_pos, dtype=np.int64),
        "b_orig": np.array(b_local, dtype=np.float64),
        "outside_local_pos": np.array(out_pos, dtype=np.int64),
        "outside_global_idx": np.array(out_gidx, dtype=np.int64),
        "outside_coeffs": np.array(out_coeffs, dtype=np.float64),
        "neighbor_ids": actual_neighbor_ids,
        "neighbor_recv_idx": neighbor_recv_idx,
        "neighbor_needed": neighbor_needed,
        "need_map": need_map,
        "a_rows": np.array(a_rows, dtype=np.int64),
        "a_cols": np.array(a_cols, dtype=np.int64),
        "a_vals": np.array(a_vals, dtype=np.float64),
        "size": size,
    }
    put_cache(setup_key, setup_data)

    solver = EXSlvSubdomainSolver.from_coo(
        size, a_rows.tolist(), a_cols.tolist(), a_vals.tolist())
    put_cache(solver_key, solver)

    INFO(f"[SETUP sd={sd}] primary={primary_size} extended={len(local_idx)} ratio={ratio:.2f}x neighbors={actual_neighbor_ids}")
    return setup_data, solver


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
    from _fly_solver import ex_slv_ras_bupdated_solve, EXSlvSubdomainSolver
    from fly import get_cache, put_cache, has_cache

    # ── Setup（内联，不调 ras_graph_setup 避免 @as_task 装饰器问题）──
    setup, solver = _do_setup(db, sd, nsd)

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
        print(f"[COMPUTE sd={sd} step={step}] sending RPC...", flush=True)
        try:
            status, resp = chan.rpc(payload, timeout=120)
        except Exception as e:
            INFO(f"[COMPUTE DAEMON sd={sd}] RPC failed at step={step}: {e}")
            break
        if status != 1:  # 1=ok, 2=error(notify_failure), 3=timeout/disconnect
            INFO(f"[COMPUTE DAEMON sd={sd}] check reported failure at step={step} status={status}")
            break

        result = pickle.loads(resp)
        if result["action"] == "done":
            INFO(f"[COMPUTE DAEMON sd={sd}] done at step={step}")
            break
        # continue：应用粗校正（xc 是 check 校正后的本子域解）
        if "xc" in result:
            xc = deserialize_array(result["xc"])
            # 不覆盖 prev_x_key（收敛检查需用上次 solve 的 x_primary）。
            # xc 单独存，供下一轮读邻居时用（coarse 模式读 xc）。
            put_cache(f"__rasg__xc_{sd}_{step}", xc)
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
                conn_id, rpc_id, src, payload = listener.accept_one(timeout=120)
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
            sol_full = db.get_full_name("__rasg__sol")
            print(f"[CHECK DAEMON] wrote sol, full_name={sol_full}", flush=True)
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


def get_ras_graph_solution_v2(db, timeout=3600):
    """轮询等 __rasg__sol（不用 @wait_obj 避免 can_still_produce 竞态）。"""
    import time as _t
    from _fly_storage import ex_stg_get_data_service
    ds = ex_stg_get_data_service()
    sol_name = db.get_full_name("__rasg__sol")
    print(f"[GET_SOL] waiting for sol_name={sol_name}", flush=True)
    deadline = _t.monotonic() + timeout
    while _t.monotonic() < deadline:
        if ds.has_local_object(sol_name) or ds.has_remote_location(sol_name):
            break
        _t.sleep(0.2)
    return {
        "x": db.read_object("__rasg__sol"),
        "iters": db.read_object("__rasg__iters"),
        "converged": db.read_object("__rasg__converged"),
    }
