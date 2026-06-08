"""Distributed RAS solver with graph-based overlap.

Algorithm: Parallel Schwarz (RAS) with graph-based overlap expansion.
No coarse correction needed — convergence via iteration change.

Task topology:
  coord (plain function, runs on master) → compute_task × nsd (dispatched to workers)
  compute_task(sd_id, step): b-update + LU solve, local convergence check
  check_task(step): read conv flags → all True? assemble : next iter
  assemble_task: merge primary solutions → final result
"""

from fly import as_task
from _fly_log import DBG, INFO
import math
import time


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


def _compute_deps(db, sd_id, step, neighbor_ids):
    deps = [db.get_obj_name(f"__rasg__setup_{sd_id}")]
    if step > 0:
        for n in neighbor_ids:
            deps.append(db.get_obj_name(f"__rasg__x_{n}_{step - 1}"))
    return deps


def _check_deps(db, step, nsd):
    return [db.get_obj_name(f"__rasg__conv_{i}_{step}") for i in range(nsd)]


# ── Phase 1: Coordination + expansion (master process) ───────────

def ras_graph_coord(db, N, rows, cols, vals, b, nsd, overlap_ratio,
                    max_iter, tol):
    from _fly_solver import (ex_slv_graph_expand_overlap,
                              ex_slv_extract_subdomain_matrix,
                              ex_slv_find_outside_connections)

    n = int(math.isqrt(N))
    primary_sets, nsd_x, nsd_y = _partition_primary_2d(n, nsd)

    if overlap_ratio <= 0:
        overlap_ratio = 0.50
    depth = _estimate_depth(n, nsd_x, nsd_y, overlap_ratio)

    global_owner = {}
    for sd_id in range(nsd):
        for gidx in primary_sets[sd_id]:
            global_owner[gidx] = sd_id

    coord = {
        "nsd": nsd, "N": N, "n": n,
        "nsd_x": nsd_x, "nsd_y": nsd_y,
        "max_iter": max_iter, "tol": tol,
        "overlap_ratio": overlap_ratio,
        "depth": depth,
        "primary_sets": primary_sets,
        "global_owner": global_owner,
        "rows": rows, "cols": cols, "vals": vals, "b": b,
    }
    db.write_object("__rasg__coord", coord, save_to_db=False)

    INFO(f"[RASG COORD] n={n} nsd={nsd} ({nsd_x}x{nsd_y}) "
         f"depth={depth} overlap_ratio={overlap_ratio:.0%}")

    neighbor_ids_all = []

    for sd_id in range(nsd):
        primary_nodes = primary_sets[sd_id]
        primary_set = set(primary_nodes)
        primary_size = len(primary_nodes)

        local_idx = ex_slv_graph_expand_overlap(
            N, rows, cols, vals, primary_nodes, depth)

        ratio = len(local_idx) / primary_size
        if ratio < 1 + overlap_ratio:
            local_idx = ex_slv_graph_expand_overlap(
                N, rows, cols, vals, primary_nodes, depth * 2)
            ratio = len(local_idx) / primary_size

        INFO(f"[RASG EXPAND] sd={sd_id} primary={primary_size} "
             f"extended={len(local_idx)} ratio={ratio:.2f}x depth={depth}")

        local_idx_map = {gidx: pos for pos, gidx in enumerate(local_idx)}
        primary_local_pos = [local_idx_map[gidx] for gidx in primary_nodes]

        size, _, a_rows, a_cols, a_vals = ex_slv_extract_subdomain_matrix(
            N, rows, cols, vals, local_idx)

        out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
            N, rows, cols, vals, local_idx)

        b_local = [b[gidx] for gidx in local_idx]

        neighbor_needed = {}
        for i, gidx in enumerate(out_gidx):
            owner = global_owner.get(gidx, -1)
            if owner >= 0 and owner != sd_id:
                if owner not in neighbor_needed:
                    neighbor_needed[owner] = []
                neighbor_needed[owner].append(i)

        neighbor_ids = sorted(neighbor_needed.keys())
        neighbor_ids_all.append(neighbor_ids)

        neighbor_recv_idx = {}
        for nb_id in neighbor_ids:
            nb_primary_map = {gidx: pos for pos, gidx in enumerate(primary_sets[nb_id])}
            recv_positions = []
            for conn_i in neighbor_needed[nb_id]:
                outside_gidx = out_gidx[conn_i]
                recv_positions.append(nb_primary_map.get(outside_gidx, -1))
            neighbor_recv_idx[nb_id] = recv_positions

        import numpy as np
        setup_data = {
            "sd_id": sd_id,
            "local_indices": np.array(local_idx, dtype=np.int64),
            "primary_local_pos": np.array(primary_local_pos, dtype=np.int64),
            "b_orig": np.array(b_local, dtype=np.float64),
            "outside_local_pos": np.array(out_pos, dtype=np.int64),
            "outside_global_idx": np.array(out_gidx, dtype=np.int64),
            "outside_coeffs": np.array(out_coeffs, dtype=np.float64),
            "neighbor_ids": neighbor_ids,
            "neighbor_recv_idx": neighbor_recv_idx,
            "neighbor_needed": neighbor_needed,
            "a_rows": np.array(a_rows, dtype=np.int64),
            "a_cols": np.array(a_cols, dtype=np.int64),
            "a_vals": np.array(a_vals, dtype=np.float64),
            "size": size,
        }
        db.write_object(f"__rasg__setup_{sd_id}", setup_data, save_to_db=True)

    cfg = {
        "nsd": nsd,
        "N": N,
        "n": n,
        "max_iter": max_iter,
        "tol": tol,
        "primary_sets": primary_sets,
        "neighbor_ids_all": neighbor_ids_all,
    }
    db.write_object("__rasg__cfg", cfg, save_to_db=False)

    INFO(f"[RASG START] nsd={nsd} launching iteration loop")

    for sd_id in range(nsd):
        ras_graph_compute(db, sd_id, 0, nsd, neighbor_ids_all[sd_id])
    ras_graph_check(db, 0, nsd, max_iter, tol, neighbor_ids_all)


# ── Compute (dispatched to workers via @as_task) ─────────────────

@as_task(inputs=lambda db, sd_id, step, nsd, neighbor_ids:
         _compute_deps(db, sd_id, step, neighbor_ids))
def ras_graph_compute(db, sd_id, step, nsd, neighbor_ids):
    from _fly_solver import EXSlvSubdomainSolver, ex_slv_ras_bupdated_solve
    from fly import get_cache, put_cache, has_cache

    cache_key = f"__rasg__setup_{sd_id}"
    if not has_cache(cache_key):
        setup = db.read_object(f"__rasg__setup_{sd_id}")
        put_cache(cache_key, setup)
    setup = get_cache(cache_key)

    solver_key = f"__rasg__solver_{sd_id}"
    if not has_cache(solver_key):
        solver = EXSlvSubdomainSolver.from_coo(
            setup["size"],
            setup["a_rows"].tolist(),
            setup["a_cols"].tolist(),
            setup["a_vals"].tolist())
        put_cache(solver_key, solver)
    solver = get_cache(solver_key)

    tol_key = "__rasg__tol"
    if not has_cache(tol_key):
        cfg = db.read_object("__rasg__cfg")
        put_cache(tol_key, cfg["tol"])
    tol = get_cache(tol_key)

    outside_coeffs = setup["outside_coeffs"].tolist()
    neighbor_values = [0.0] * len(outside_coeffs)
    if step > 0:
        for nb_id in setup["neighbor_ids"]:
            nb_x = db.read_object(f"__rasg__x_{nb_id}_{step - 1}")
            recv_positions = setup["neighbor_recv_idx"][nb_id]
            conn_indices = setup["neighbor_needed"][nb_id]
            for i, conn_i in enumerate(conn_indices):
                pos = recv_positions[i]
                if pos >= 0:
                    neighbor_values[conn_i] = nb_x[pos]

    x_local = ex_slv_ras_bupdated_solve(
        solver, setup["b_orig"].tolist(),
        setup["outside_local_pos"].tolist(), outside_coeffs,
        neighbor_values)

    primary_local_pos = setup["primary_local_pos"].tolist()
    x_primary = [x_local[pos] for pos in primary_local_pos]

    prev_x_key = f"__rasg__prev_x_{sd_id}"
    converged_local = False
    if step > 0 and has_cache(prev_x_key):
        prev_x = get_cache(prev_x_key)
        max_delta = max(abs(a - b) for a, b in zip(x_primary, prev_x))
        converged_local = max_delta < tol
    put_cache(prev_x_key, list(x_primary))

    import numpy as np
    db.write_object(f"__rasg__x_{sd_id}_{step}",
                    np.array(x_primary, dtype=np.float64), save_to_db=True)
    db.write_object(f"__rasg__conv_{sd_id}_{step}", converged_local,
                    save_to_db=True)

    if step > 1:
        try:
            db.remove_object(f"__rasg__x_{sd_id}_{step - 2}")
        except Exception:
            pass

    DBG(f"[RASG COMPUTE] sd={sd_id} step={step} conv_local={converged_local}")


# ── Check ────────────────────────────────────────────────────────

@as_task(inputs=lambda db, step, nsd, max_iter, tol, neighbor_ids_all:
         _check_deps(db, step, nsd))
def ras_graph_check(db, step, nsd, max_iter, tol, neighbor_ids_all):
    conv_flags = [db.read_object(f"__rasg__conv_{i}_{step}") for i in range(nsd)]
    all_converged = all(conv_flags)

    DBG(f"[RASG CHECK] step={step} converged={all_converged} "
        f"flags={conv_flags}")

    if all_converged or step >= max_iter - 1:
        db.write_object("__rasg__converged", all_converged, save_to_db=False)
        db.write_object("__rasg__iters", step + 1, save_to_db=False)
        ras_graph_assemble(db, nsd, step)
    else:
        for sd_id in range(nsd):
            ras_graph_compute(db, sd_id, step + 1, nsd,
                              neighbor_ids_all[sd_id])
        ras_graph_check(db, step + 1, nsd, max_iter, tol, neighbor_ids_all)

    for i in range(nsd):
        try:
            db.remove_object(f"__rasg__conv_{i}_{step}")
        except Exception:
            pass


# ── Assemble ─────────────────────────────────────────────────────

@as_task(inputs=lambda db, nsd, final_step: [
    db.get_obj_name(f"__rasg__x_{i}_{final_step}") for i in range(nsd)
])
def ras_graph_assemble(db, nsd, final_step):
    cfg = db.read_object("__rasg__cfg")
    N = cfg["N"]
    primary_sets = cfg["primary_sets"]

    x = [0.0] * N
    for sd_id in range(nsd):
        x_sd = db.read_object(f"__rasg__x_{sd_id}_{final_step}")
        for pos, gidx in enumerate(primary_sets[sd_id]):
            x[gidx] = x_sd[pos]

    db.write_object("__rasg__sol", x, save_to_db=True)
    DBG(f"[RASG ASSEMBLE] nsd={nsd} final_step={final_step}")


# ── Public API ────────────────────────────────────────────────────

def get_ras_graph_solution(db, timeout=3600):
    sol_name = db.get_obj_name("__rasg__sol")
    from _fly_storage import ex_stg_get_data_service
    ds = ex_stg_get_data_service()
    t0 = time.time()
    while time.time() - t0 < timeout:
        if ds.has_local_object(sol_name) or ds.has_remote_location(sol_name):
            return {
                "x": db.read_object("__rasg__sol"),
                "iters": db.read_object("__rasg__iters"),
                "converged": db.read_object("__rasg__converged"),
            }
        time.sleep(0.5)
    raise RuntimeError(f"get_ras_graph_solution: timed out waiting for __rasg__sol after {timeout}s")


def solve_ras_graph(db, N, rows, cols, vals, b, nsd,
                    overlap_ratio=0.50, max_iter=100, tol=1e-8):
    ras_graph_coord(db, N, rows, cols, vals, b, nsd,
                    overlap_ratio, max_iter, tol)
    return get_ras_graph_solution(db)
