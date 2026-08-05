"""RAS solver v2（常驻 daemon + RPC 直连 + 分块 setup + 粗校正内联）。

三项完整优化（vs 之前"半优化"版本）：
1. 分块 setup：coord 预提取每子域矩阵数据，compute 只读自己的子块（跳过全量矩阵/BFS/rank-filter）
2. 粗校正内联：check 不再调 _apply_coarse_correction（它走 DB），改为内联粗校正逻辑
   （assemble 从 RPC 收到的 contributions 直接组装，residual + coarse solve 在内存，
   write 改 RPC 回复），完全不经 DB
3. 邻居解走 RPC：compute 读邻居从上轮 RPC 返回的 xc 缓存（而非 DB read_object）
"""
import time
import pickle
import numpy as np
from _fly_log import DBG, INFO, WARN, ERR
from fly import as_task
from agent import PeerChannelGroup, serialize_array, deserialize_array


def solve_ras_graph_v2(db, matrix_path, nsd,
                       overlap_ratio=0.50, max_iter=100, tol=1e-8,
                       omega=1.0, max_concurrent_compute=None):
    """nsd+1 worker（1 check + nsd compute），常驻 daemon + RPC 直连。"""
    from fly.runtime import get_agent

    n_workers = nsd + 1
    master = get_agent()
    if not master.is_running() or master.worker_count < n_workers:
        worker_configs = [{"attributes": ["check"]}]
        for w in range(nsd):
            worker_configs.append({"attributes": [f"sd_{w}"]})
        master.launch_local_workers(worker_configs)
        assert master.wait_for_workers(n_workers), f"{n_workers} workers should connect"

    INFO(f"[RASG V2] nsd={nsd} n_workers={n_workers}")
    _coord_prebuild(db, matrix_path, nsd, overlap_ratio, max_iter, tol, omega)

    group = PeerChannelGroup()
    INFO(f"[RASG V2] group_id={group.group_id[:8]}")

    check_daemon_task(db, group.group_id, nsd, max_iter, tol, omega)
    for sd in range(nsd):
        compute_daemon_task(db, group.group_id, sd, nsd, omega)

    return _wait_solution(db)


def _coord_prebuild(db, matrix_path, nsd, overlap_ratio, max_iter, tol, omega):
    """coord 预构建 + 分块提取：coord 一次性做矩阵加载/分区/coarse 预构建 +
    每子域 BFS/rank-filter/LDLT 数据提取，发布到 DB。compute 直接读子域块。"""
    from solver.ras_graph import (_load_matrix, _partition_primary_2d,
                                   _estimate_depth, _compute_grid_neighbors,
                                   _prebuild_coarse_in_coord, _prebuild_coarse_grid,
                                   _get_matrix_data)
    import scipy.sparse as sp
    from scipy.sparse.linalg import splu

    data = _load_matrix(matrix_path)
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
        "matrix_path": matrix_path,
    }
    db.write_object("__rasg__coord", coord, save_to_db=False)
    cfg = {
        "nsd": nsd, "N": N, "n": n, "max_iter": max_iter, "tol": tol,
        "omega": omega, "primary_sets": primary_sets,
        "neighbor_ids_all": neighbor_ids_all, "matrix_path": matrix_path,
    }
    db.write_object("__rasg__cfg", cfg, save_to_db=False)

    INFO(f"[RASG V2 COORD] n={n} nsd={nsd} ({nsd_x}x{nsd_y}) depth={depth}")

    # ── 分块提取：coord 一次性为每子域做 BFS + rank-filter ──
    rows_arr = np.asarray(rows); cols_arr = np.asarray(cols); vals_arr = np.asarray(vals)
    # 构建 adjacency（共享）
    _si = np.argsort(cols_arr, kind="stable")
    adj_starts = np.searchsorted(cols_arr[_si], np.arange(N + 1))
    adj_rows_sorted = rows_arr[_si]

    def _bfs(seed, layers):
        expanded = set(seed); current = list(seed)
        for _ in range(layers):
            frontier = set()
            for node in current:
                s, e = adj_starts[node], adj_starts[node + 1]
                for row in adj_rows_sorted[s:e]:
                    if row != node and row not in expanded:
                        frontier.add(int(row))
            if not frontier: break
            expanded |= frontier; current = frontier
        return sorted(expanded)

    for sd in range(nsd):
        primary_nodes = primary_sets[sd]
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
        need_map = {}
        for nb_id in actual_neighbor_ids:
            nb_pm = {g: p for p, g in enumerate(primary_sets[nb_id])}
            recv_positions = []; need_global = []
            for ci in neighbor_needed[nb_id]:
                og = int(out_gidx[ci])
                recv_positions.append(nb_pm.get(og, -1))
                need_global.append(og)
            neighbor_recv_idx[nb_id] = recv_positions
            need_map[nb_id] = np.array(need_global, dtype=np.int64)

        subdomain_data = {
            "sd_id": sd,
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
        INFO(f"[RASG V2 COORD] subdomain {sd}: primary={len(primary_nodes)} extended={len(local_idx)} ratio={ratio:.2f}x neighbors={actual_neighbor_ids}")

    # coarse 预构建
    if omega == "coarse":
        _prebuild_coarse_in_coord(db, n, N, matrix_path)
        _prebuild_coarse_grid(db, nsd)


@as_task(requires=lambda db, group_id, sd, nsd, omega: [f"sd_{sd}"])
def compute_daemon_task(db, group_id, sd, nsd, omega_strategy):
    """常驻 compute：读预分块子域数据 → LDLT setup → connect check → while solve + RPC。"""
    from _fly_solver import ex_slv_ras_bupdated_solve, EXSlvSubdomainSolver
    from fly import get_cache, put_cache, has_cache

    # ── 读预分块子域数据（coord 已做 BFS/rank-filter）──
    sub = db.read_object(f"__rasg__sub_{sd}")
    cfg = db.read_object("__rasg__cfg")
    tol = cfg["tol"]

    # LDLT 分解（唯一不可省的计算）
    solver = EXSlvSubdomainSolver.from_coo(
        sub["size"], sub["a_rows"].tolist(), sub["a_cols"].tolist(), sub["a_vals"].tolist())
    setup = sub  # sub 就是 setup（coord 预提取的）
    INFO(f"[COMPUTE sd={sd}] LDLT setup done, size={sub['size']}")

    # ── Connect check ──
    group = PeerChannelGroup(group_id)
    chan = group.connect(db, timeout=120)
    INFO(f"[COMPUTE sd={sd}] connected to check")

    # ── 迭代循环 ──
    step = 0
    use_coarse = (omega_strategy == "coarse")
    xc_cache = {}  # {sd: xc_array} 上轮粗校正后各子域的解（从 RPC 回复缓存）
    while True:
        # 读邻居解（从上轮 RPC 缓存的 xc，而非 DB）
        outside_coeffs = setup["outside_coeffs"]
        neighbor_values = [0.0] * len(outside_coeffs)
        if step > 0:
            for nb_id in setup["neighbor_ids"]:
                # 优先从 xc_cache（RPC 回复缓存）
                if nb_id in xc_cache:
                    nb_x = xc_cache[nb_id]
                else:
                    nb_x = np.zeros(len(cfg["primary_sets"][nb_id]))  # fallback
                recv_positions = setup["neighbor_recv_idx"][nb_id]
                conn_indices = setup["neighbor_needed"][nb_id]
                for i, conn_i in enumerate(conn_indices):
                    pos = recv_positions[i]
                    if pos >= 0 and pos < len(nb_x):
                        neighbor_values[conn_i] = nb_x[pos]

        # 本地 solve
        x_local = ex_slv_ras_bupdated_solve(
            solver, setup["b_orig"],
            setup["outside_local_pos"], outside_coeffs,
            neighbor_values, 1.0)
        x_primary = np.asarray(x_local, dtype=np.float64)[setup["primary_local_pos"]]

        # 收敛检查
        converged_local = False
        prev_x_key = f"__rasg__prev_x_{sd}"
        if step > 0 and has_cache(prev_x_key):
            prev_x = np.asarray(get_cache(prev_x_key), dtype=np.float64)
            max_delta = float(np.max(np.abs(x_primary - prev_x)))
            converged_local = max_delta < tol
        put_cache(prev_x_key, x_primary)

        # RPC 发本轮解给 check
        payload = pickle.dumps({
            "sd": sd, "step": step, "conv": converged_local,
            "x": serialize_array(x_primary),
        })
        try:
            status, resp = chan.rpc(payload, timeout=120)
        except Exception as e:
            INFO(f"[COMPUTE sd={sd}] RPC failed at step={step}: {e}")
            break
        if status != 1:
            INFO(f"[COMPUTE sd={sd}] check failure at step={step} status={status}")
            break

        result = pickle.loads(resp)
        if result["action"] == "done":
            INFO(f"[COMPUTE sd={sd}] done at step={step}")
            break

        # continue：缓存校正后的解（供下轮读邻居）
        if "xc_all" in result:
            # check 发回所有子域的校正解（compute 用它读邻居）
            xc_all = pickle.loads(result["xc_all"])
            for k, v_bytes in xc_all.items():
                xc_cache[int(k)] = deserialize_array(v_bytes)
            # 更新自己的 prev 为校正后的解
            if sd in xc_cache:
                put_cache(prev_x_key, xc_cache[sd])
        step += 1

    chan.close()
    INFO(f"[COMPUTE sd={sd}] exited at step={step}")


@as_task(requires=lambda db, group_id, nsd, max_iter, tol, omega: ["check"])
def check_daemon_task(db, group_id, nsd, max_iter, tol, omega_strategy):
    """常驻 check：listen → accept → 收齐 nsd 份 → 内联粗校正 → 回复 → 循环。

    粗校正完全内联（不经 DB）：assemble 从 RPC 数据直接组装，residual + coarse solve
    在内存，回复走 RPC。
    """
    import scipy.sparse as sp
    from scipy.sparse.linalg import splu
    from fly import get_cache, has_cache

    group = PeerChannelGroup(group_id)
    listener = group.listen(db)
    INFO(f"[CHECK] listening port={listener.port}")

    coord = db.read_object("__rasg__coord")
    N = coord["N"]
    n = coord["n"]
    primary_sets = coord["primary_sets"]
    matrix_path = coord["matrix_path"]
    use_coarse = (omega_strategy == "coarse")

    # ── 粗校正预构建（内存缓存，不经 DB 读写）──
    Ac_lu = None; P = None; A_fine = None; b_fine = None
    if use_coarse:
        from solver.ras_graph import _compute_coarse_arrays, _ensure_coarse_cached
        _ensure_coarse_cached(db)
        if has_cache("__rasg__coarse_lu"):
            Ac_lu = get_cache("__rasg__coarse_lu")
            P = get_cache("__rasg__coarse_P")
            b_fine = get_cache("__rasg__coarse_b")
        # A_fine 需构建一次（用于 residual r = b - A·x）
        if not has_cache("__rasg__coarse_A"):
            from solver.ras_graph import _get_matrix_data
            md = _get_matrix_data(matrix_path)
            A_fine = sp.csr_matrix(
                (np.asarray(md["vals"]), (np.asarray(md["rows"]), np.asarray(md["cols"]))),
                shape=(N, N))
            from fly import put_cache
            put_cache("__rasg__coarse_A", A_fine)
        else:
            A_fine = get_cache("__rasg__coarse_A")

    # primary_sets 的 numpy 索引数组（预计算，避免每迭代转换）
    ps_arrays = [np.asarray(ps) for ps in primary_sets]

    step = 0
    while step < max_iter:
        # ── 收齐 nsd 份 ──
        contributions = {}
        failed = False
        for _ in range(nsd):
            try:
                conn_id, rpc_id, src, payload = listener.accept_one(timeout=120)
            except Exception:
                failed = True; break
            if rpc_id == 0:
                failed = True; break
            data = pickle.loads(payload)
            sd = data["sd"]
            contributions[sd] = {
                "conn_id": conn_id, "rpc_id": rpc_id,
                "x": deserialize_array(data["x"]), "conv": data["conv"],
            }

        if failed or len(contributions) < nsd:
            ERR(f"[CHECK] only {len(contributions)}/{nsd} at step={step}")
            for sd, c in contributions.items():
                listener.notify_failure(c["conn_id"], "peer timeout")
            break

        all_converged = all(c["conv"] for c in contributions.values())

        # check 侧残差判定（compute 的 conv 可能因粗校正干扰不触发，用全局残差兜底）
        x_global_check = np.zeros(N, dtype=np.float64)
        for sd_idx in range(nsd):
            x_global_check[ps_arrays[sd_idx]] = contributions[sd_idx]["x"]
        if use_coarse and Ac_lu is not None:
            r_norm = float(np.linalg.norm(b_fine - A_fine.dot(x_global_check)))
            r_rel = r_norm / max(float(np.linalg.norm(b_fine)), 1e-30)
            if r_rel < tol:
                all_converged = True
        if step >= 5:  # 至少迭代 5 步才允许残差收敛判定（前几步残差可能假小）
            pass
        else:
            all_converged = False

        if all_converged or step == max_iter - 1:
            # ── 收敛：assemble sol + done ──
            x_global = np.zeros(N, dtype=np.float64)
            for sd in range(nsd):
                x_global[ps_arrays[sd]] = contributions[sd]["x"]
            db.write_object("__rasg__sol", x_global)
            db.write_object("__rasg__iters", step + 1)
            db.write_object("__rasg__converged", all_converged)
            for sd in range(nsd):
                c = contributions[sd]
                listener.respond(c["conn_id"], c["rpc_id"], pickle.dumps({"action": "done"}))
            INFO(f"[CHECK] converged={all_converged} at step={step}")
            break

        # ── 内联粗校正（完全在内存，不经 DB）──
        t_coarse = time.perf_counter()
        # assemble 全局解
        x_global = np.zeros(N, dtype=np.float64)
        for sd in range(nsd):
            x_global[ps_arrays[sd]] = contributions[sd]["x"]

        if use_coarse and Ac_lu is not None:
            # residual
            r = b_fine - A_fine.dot(x_global)
            # coarse solve
            e_c = Ac_lu.solve(P.T.dot(r))
            e_fine = P.dot(e_c)
            # 校正
            x_corrected = x_global + e_fine
            # 提取每子域的校正后解
            xc_all = {}
            for sd in range(nsd):
                xc_sd = x_corrected[ps_arrays[sd]]
                xc_all[sd] = serialize_array(xc_sd)
            t_coarse_ms = (time.perf_counter() - t_coarse) * 1000
            r_norm = float(np.linalg.norm(r))
            INFO(f"[CHECK] coarse step={step} t={t_coarse_ms:.0f}ms |r|={r_norm:.2e}")
        else:
            xc_all = {sd: serialize_array(contributions[sd]["x"]) for sd in range(nsd)}

        # ── 回复各 compute（continue + 所有子域校正解）──
        xc_all_bytes = pickle.dumps(xc_all)
        for sd in range(nsd):
            c = contributions[sd]
            listener.respond(c["conn_id"], c["rpc_id"],
                             pickle.dumps({"action": "continue", "xc_all": xc_all_bytes}))
        step += 1

    listener.close()
    INFO(f"[CHECK] exited at step={step}")


def _wait_solution(db, timeout=3600):
    """轮询等 __rasg__sol（不用 @wait_obj 避免 can_still_produce 竞态）。"""
    from _fly_storage import ex_stg_get_data_service
    ds = ex_stg_get_data_service()
    sol_name = db.get_full_name("__rasg__sol")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if ds.has_local_object(sol_name) or ds.has_remote_location(sol_name):
            break
        time.sleep(0.2)
    return {
        "x": db.read_object("__rasg__sol"),
        "iters": db.read_object("__rasg__iters"),
        "converged": db.read_object("__rasg__converged"),
    }
