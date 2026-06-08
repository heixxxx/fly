"""Distributed RAS solver with graph-based overlap.

Algorithm: Parallel Schwarz (RAS) with graph-based overlap expansion.
Optional two-level coarse grid correction for faster convergence.

Task topology:
  coord (plain function, runs on master) → compute_task × nsd (dispatched to workers)
  compute_task(sd_id, step): b-update + LU solve, local convergence check
  check_task(step): coarse correction (if enabled) → convergence check → next iter
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


def _is_adaptive(db):
    try:
        cfg = db.read_object("__rasg__cfg")
        return cfg.get("omega", 1.0) == "adaptive"
    except Exception:
        return False


def _is_coarse(db):
    try:
        cfg = db.read_object("__rasg__cfg")
        omega = cfg.get("omega", 1.0)
        return isinstance(omega, str) and "coarse" in omega
    except Exception:
        return False


def _compute_deps(db, sd_id, step, neighbor_ids):
    deps = [db.get_obj_name(f"__rasg__setup_{sd_id}")]
    if step > 0:
        use_coarse = _is_coarse(db)
        for n in neighbor_ids:
            if use_coarse:
                deps.append(db.get_obj_name(f"__rasg__xc_{n}_{step - 1}"))
            else:
                deps.append(db.get_obj_name(f"__rasg__x_{n}_{step - 1}"))
        if _is_adaptive(db):
            deps.append(db.get_obj_name(f"__rasg__gomega_{step}"))
    return deps


def _check_deps(db, step, nsd):
    deps = [db.get_obj_name(f"__rasg__conv_{i}_{step}") for i in range(nsd)]
    if step > 0 and _is_adaptive(db):
        for i in range(nsd):
            deps.append(db.get_obj_name(f"__rasg__err_{i}_{step}"))
    return deps


def _aitken_omega(delta_curr, delta_prev):
    """Aitken delta-squared adaptive relaxation.

    Given two consecutive solution deltas on the primary region:
        delta_curr = x_{k-1} - x_{k-2}
        delta_prev = x_{k-2} - x_{k-3}

    Compute the optimal relaxation parameter:
        omega = (delta_curr · delta_curr) / (delta_curr · (delta_curr - delta_prev))

    This is the standard Aitken Δ² acceleration adapted for
    Schwarz-type fixed-point iterations (Dufaud & Tromeur-Dervout, 2010).

    Returns omega clipped to [0.3, 2.0] for stability.
    """
    # Compute dot products component-wise for local omega estimate
    numer = sum(c * c for c in delta_curr)
    diff = [c - p for c, p in zip(delta_curr, delta_prev)]
    denom = sum(c * d for c, d in zip(delta_curr, diff))
    if abs(denom) < 1e-30:
        return 1.0
    omega = numer / denom
    return max(0.3, min(2.0, omega))


# ── Phase 1: Coordination + expansion (master process) ───────────

def ras_graph_coord(db, N, rows, cols, vals, b, nsd, overlap_ratio,
                    max_iter, tol, omega=1.0):
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
        db.write_object(f"__rasg__setup_{sd_id}", setup_data, save_to_db=False)

    cfg = {
        "nsd": nsd,
        "N": N,
        "n": n,
        "max_iter": max_iter,
        "tol": tol,
        "omega": omega,
        "primary_sets": primary_sets,
        "neighbor_ids_all": neighbor_ids_all,
    }
    db.write_object("__rasg__cfg", cfg, save_to_db=False)

    INFO(f"[RASG START] nsd={nsd} omega={omega} launching iteration loop")

    for sd_id in range(nsd):
        ras_graph_compute(db, sd_id, 0, nsd, neighbor_ids_all[sd_id])
    ras_graph_check(db, 0, nsd, max_iter, tol, neighbor_ids_all)


# ── Coarse Grid Correction ──────────────────────────────────────────

def _ensure_coarse_cached(db):
    """Lazy-build coarse grid operators and cache in process-local cache.
    Called from worker's check task. Reads __rasg__coord (save_to_db=False,
    readable cross-worker) for the fine-grid matrix.
    """
    from fly import get_cache, has_cache, put_cache
    if has_cache("__rasg__coarse_lu"):
        return

    import numpy as np
    from scipy import sparse
    from scipy.sparse.linalg import splu

    coord = db.read_object("__rasg__coord")
    N = coord["N"]
    n = coord["n"]
    rows = coord["rows"]
    cols = coord["cols"]
    vals = coord["vals"]

    stride = max(2, n // 125)
    nc = n // stride
    Nc = nc * nc

    if Nc < 10:
        INFO(f"[RASG COARSE] Skipping: coarse grid too small (nc={nc})")
        return

    # Build prolongation P: N × Nc, bilinear interpolation
    P_rows, P_cols, P_vals = [], [], []
    for fi in range(n):
        for fj in range(n):
            fine_idx = fi * n + fj
            ci_f = fi / stride
            cj_f = fj / stride
            ci0 = min(int(ci_f), nc - 1)
            cj0 = min(int(cj_f), nc - 1)
            di = ci_f - ci0
            dj = cj_f - cj0
            ci1 = min(ci0 + 1, nc - 1)
            cj1 = min(cj0 + 1, nc - 1)

            if ci0 == ci1 and cj0 == cj1:
                P_rows.append(fine_idx)
                P_cols.append(ci0 * nc + cj0)
                P_vals.append(1.0)
            else:
                for ci, cj, w in [
                    (ci0, cj0, (1 - di) * (1 - dj)),
                    (ci0, cj1, (1 - di) * dj),
                    (ci1, cj0, di * (1 - dj)),
                    (ci1, cj1, di * dj),
                ]:
                    P_rows.append(fine_idx)
                    P_cols.append(ci * nc + cj)
                    P_vals.append(w)

    P = sparse.csr_matrix((P_vals, (P_rows, P_cols)), shape=(N, Nc))
    A_fine = sparse.csr_matrix((vals, (rows, cols)), shape=(N, N))

    Ac = (P.T @ (A_fine @ P)).tocsc()
    Ac_lu = splu(Ac)

    put_cache("__rasg__coarse_lu", Ac_lu)
    put_cache("__rasg__coarse_P", P)
    put_cache("__rasg__coarse_A", A_fine)
    put_cache("__rasg__coarse_b", np.array(coord["b"], dtype=np.float64))
    put_cache("__rasg__coarse_stride", stride)
    INFO(f"[RASG COARSE] Built on worker: stride={stride} nc={nc} "
         f"Nc={Nc} nnz={Ac.nnz}")


def _apply_coarse_correction(db, step, nsd):
    """Apply coarse grid correction after local RAS solves.

    Assembles global x from subdomains, computes residual r = b - Ax,
    solves coarse system, and distributes correction to subdomain x data.
    All computation happens on the worker running the check task.
    """
    import numpy as np
    from fly import get_cache

    _ensure_coarse_cached(db)

    from fly import has_cache
    if not has_cache("__rasg__coarse_lu"):
        return

    Ac_lu = get_cache("__rasg__coarse_lu")
    P = get_cache("__rasg__coarse_P")
    A_fine = get_cache("__rasg__coarse_A")
    b_fine = get_cache("__rasg__coarse_b")

    cfg = db.read_object("__rasg__cfg")
    N = cfg["N"]
    primary_sets = cfg["primary_sets"]

    # Assemble global x from subdomain primary data
    x_global = np.zeros(N, dtype=np.float64)
    for sd_id in range(nsd):
        x_sd = db.read_object(f"__rasg__x_{sd_id}_{step}")
        for pos, gidx in enumerate(primary_sets[sd_id]):
            x_global[gidx] = x_sd[pos]

    # Compute residual r = b - Ax (A_fine cached, no rebuild)
    r = b_fine - A_fine.dot(x_global)
    r_norm = np.linalg.norm(r)

    # Restrict → solve coarse → interpolate
    e_c = Ac_lu.solve(P.T.dot(r))
    e_fine = P.dot(e_c)
    e_norm = np.linalg.norm(e_fine)

    # Apply correction: write corrected x as __rasg__xc_{sd_id}_{step}
    # (separate key to avoid write provenance mismatch with compute task)
    for sd_id in range(nsd):
        x_sd = db.read_object(f"__rasg__x_{sd_id}_{step}")
        corrected = list(x_sd)
        for pos, gidx in enumerate(primary_sets[sd_id]):
            corrected[pos] += e_fine[gidx]
        db.write_object(f"__rasg__xc_{sd_id}_{step}",
                        np.array(corrected, dtype=np.float64), save_to_db=False)

    INFO(f"[RASG COARSE] step={step} |r|={r_norm:.2e} |e|={e_norm:.2e}")


# ── Compute (dispatched to workers via @as_task) ─────────────────

@as_task(inputs=lambda db, sd_id, step, nsd, neighbor_ids:
         _compute_deps(db, sd_id, step, neighbor_ids),
         requires=lambda db, sd_id, step, nsd, neighbor_ids:
         [f"sd_{sd_id}"])
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
    omega_key = "__rasg__omega"
    if not has_cache(tol_key):
        cfg = db.read_object("__rasg__cfg")
        put_cache(tol_key, cfg["tol"])
        put_cache(omega_key, cfg.get("omega", 1.0))
    tol = get_cache(tol_key)
    omega_strategy = get_cache(omega_key)

    outside_coeffs = setup["outside_coeffs"].tolist()
    neighbor_values = [0.0] * len(outside_coeffs)
    use_coarse = _is_coarse(db)
    if step > 0:
        x_prefix = "__rasg__xc_" if use_coarse else "__rasg__x_"
        for nb_id in setup["neighbor_ids"]:
            nb_x = db.read_object(f"{x_prefix}{nb_id}_{step - 1}")
            recv_positions = setup["neighbor_recv_idx"][nb_id]
            conn_indices = setup["neighbor_needed"][nb_id]
            for i, conn_i in enumerate(conn_indices):
                pos = recv_positions[i]
                if pos >= 0:
                    neighbor_values[conn_i] = nb_x[pos]

    prev_x_key = f"__rasg__prev_x_{sd_id}"
    prev2_x_key = f"__rasg__prev2_x_{sd_id}"
    prev3_x_key = f"__rasg__prev3_x_{sd_id}"

    omega = 1.0
    if omega_strategy == "adaptive":
        if step > 0:
            global_omega_key = f"__rasg__gomega_{step}"
            omega = db.read_object(global_omega_key)
        else:
            omega = 1.0
    elif omega_strategy == "aitken":
        if step >= 3 and has_cache(prev2_x_key) and has_cache(prev3_x_key):
            prev_x = get_cache(prev_x_key)
            prev2_x = get_cache(prev2_x_key)
            prev3_x = get_cache(prev3_x_key)
            delta_curr = [a - b for a, b in zip(prev_x, prev2_x)]
            delta_prev = [a - b for a, b in zip(prev2_x, prev3_x)]
            omega = _aitken_omega(delta_curr, delta_prev)
        else:
            omega = 1.0
    elif isinstance(omega_strategy, (int, float)):
        omega = float(omega_strategy)

    x_local = ex_slv_ras_bupdated_solve(
        solver, setup["b_orig"].tolist(),
        setup["outside_local_pos"].tolist(), outside_coeffs,
        neighbor_values, 1.0)

    primary_local_pos = setup["primary_local_pos"].tolist()
    x_primary = [x_local[pos] for pos in primary_local_pos]

    if step > 0 and omega != 1.0 and has_cache(prev_x_key):
        prev_x = get_cache(prev_x_key)
        x_primary = [(1.0 - omega) * p + omega * n
                     for p, n in zip(prev_x, x_primary)]

    converged_local = False
    max_delta = 0.0
    if step > 0 and has_cache(prev_x_key):
        prev_x = get_cache(prev_x_key)
        max_delta = max(abs(a - b) for a, b in zip(x_primary, prev_x))
        converged_local = max_delta < tol

    if has_cache(prev2_x_key):
        put_cache(prev3_x_key, get_cache(prev2_x_key))
    if has_cache(prev_x_key):
        put_cache(prev2_x_key, get_cache(prev_x_key))
    put_cache(prev_x_key, list(x_primary))

    import numpy as np
    db.write_object(f"__rasg__x_{sd_id}_{step}",
                    np.array(x_primary, dtype=np.float64), save_to_db=False)
    db.write_object(f"__rasg__conv_{sd_id}_{step}", converged_local,
                    save_to_db=False)
    if step > 0 and omega_strategy == "adaptive":
        db.write_object(f"__rasg__err_{sd_id}_{step}", max_delta,
                        save_to_db=False)

    if step > 1:
        try:
            db.remove_object(f"__rasg__x_{sd_id}_{step - 2}")
        except Exception:
            pass
        if _is_coarse(db):
            try:
                db.remove_object(f"__rasg__xc_{sd_id}_{step - 2}")
            except Exception:
                pass

    DBG(f"[RASG COMPUTE] sd={sd_id} step={step} omega={omega:.4f} "
        f"conv_local={converged_local}")


# ── Check ────────────────────────────────────────────────────────

def _compute_adaptive_omega(errs, tol, prev_err=None):
    if not errs or max(errs) < 1e-30:
        return 1.0
    import math
    global_max = max(errs)
    ratio = math.log10(global_max / tol) if global_max > tol else 0.0
    if ratio < 1.0:
        return 1.0
    if prev_err is not None and global_max > prev_err * 2.0:
        return 1.0
    max_omega = 1.5 if ratio > 3.0 else (1.2 if ratio > 2.0 else 1.1)
    omega = 1.0 + (max_omega - 1.0) * min(1.0, ratio / 6.0)
    return min(max_omega, omega)


@as_task(inputs=lambda db, step, nsd, max_iter, tol, neighbor_ids_all:
         _check_deps(db, step, nsd))
def ras_graph_check(db, step, nsd, max_iter, tol, neighbor_ids_all):
    conv_flags = [db.read_object(f"__rasg__conv_{i}_{step}") for i in range(nsd)]
    all_converged = all(conv_flags)

    cfg = db.read_object("__rasg__cfg")
    omega_strategy = cfg.get("omega", 1.0)
    use_coarse = _is_coarse(db)

    # ── Coarse grid correction (before convergence check) ──
    if use_coarse and not all_converged and step < max_iter - 1:
        _apply_coarse_correction(db, step, nsd)

    if not all_converged and 0 < step < max_iter - 1 and omega_strategy == "adaptive":
        import math
        errs = [db.read_object(f"__rasg__err_{i}_{step}") for i in range(nsd)]
        prev_err_key = f"__rasg__prev_err_{step - 1}"
        prev_err = None
        try:
            prev_err = db.read_object(prev_err_key)
        except Exception:
            pass
        next_omega = _compute_adaptive_omega(errs, tol, prev_err)
        global_max = max(errs)
        db.write_object(f"__rasg__prev_err_{step}", global_max,
                        save_to_db=False)
        db.write_object(f"__rasg__gomega_{step + 1}", next_omega,
                        save_to_db=False)
        INFO(f"[RASG ADAPTIVE] step={step} gomega={next_omega:.4f} "
             f"max_err={global_max:.2e} ratio={math.log10(global_max/tol):.1f}"
             f"{' [DIVERGE]' if prev_err and global_max > prev_err * 1.1 else ''}")
    elif not all_converged and step == 0 and omega_strategy == "adaptive":
        db.write_object(f"__rasg__gomega_1", 1.5,
                        save_to_db=False)
        INFO(f"[RASG ADAPTIVE] step=0 gomega=1.5000 (initial)")

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
    if omega_strategy == "adaptive":
        for i in range(nsd):
            if step > 0:
                try:
                    db.remove_object(f"__rasg__err_{i}_{step}")
                except Exception:
                    pass
        if step > 1:
            try:
                db.remove_object(f"__rasg__gomega_{step - 1}")
            except Exception:
                pass
            try:
                db.remove_object(f"__rasg__prev_err_{step - 2}")
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
    use_coarse = _is_coarse(db)

    x = [0.0] * N
    for sd_id in range(nsd):
        # Use coarse-corrected data if available, else raw x
        if use_coarse:
            xc_name = db.get_obj_name(f"__rasg__xc_{sd_id}_{final_step}")
            from _fly_storage import ex_stg_get_data_service
            ds = ex_stg_get_data_service()
            if ds.has_local_object(xc_name) or ds.has_remote_location(xc_name):
                x_sd = db.read_object(f"__rasg__xc_{sd_id}_{final_step}")
            else:
                x_sd = db.read_object(f"__rasg__x_{sd_id}_{final_step}")
        else:
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
                    overlap_ratio=0.50, max_iter=100, tol=1e-8,
                    omega=1.0):
    from fly.runtime import get_agent

    master = get_agent()
    if not master.is_running() or master.worker_count < nsd:
        worker_configs = [{"attributes": [f"sd_{i}"]} for i in range(nsd)]
        master.launch_local_workers(worker_configs)
        assert master.wait_for_workers(nsd), f"{nsd} workers should connect"

    ras_graph_coord(db, N, rows, cols, vals, b, nsd,
                    overlap_ratio, max_iter, tol, omega)
    return get_ras_graph_solution(db)
