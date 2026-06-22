"""Distributed RAS solver with graph-based overlap.

Algorithm: Parallel Schwarz (RAS) with graph-based overlap expansion.
Optional two-level coarse grid correction for faster convergence.

Task topology:
  coord (plain function, runs on master) → compute_task × nsd (dispatched to workers)
  compute_task(sd_id, step): load matrix + expand + extract + b-update + LU solve
  check_task(step): coarse correction (if enabled) → convergence check → next iter
  assemble_task: merge primary solutions → final result
"""

from fly import as_task, wait_obj
from _fly_log import DBG, INFO
import math
import time
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu


# ── Matrix File I/O ──────────────────────────────────────────────

def generate_poisson_matrix(n, path):
    """Generate a Poisson 2D matrix and save to .npz file.

    Creates a 5-point stencil Laplacian on an n×n grid.
    RHS b = [1.0] * N. Golden solution computed via scipy.sparse.linalg.splu.

    Args:
        n: Grid side length (matrix is n² × n²)
        path: Output .npz file path
    """
    import numpy as np
    from scipy import sparse
    from scipy.sparse.linalg import splu

    N = n * n
    diags = [4.0 * np.ones(N),
             -1.0 * np.ones(N - 1), -1.0 * np.ones(N - 1),
             -1.0 * np.ones(N - n), -1.0 * np.ones(N - n)]
    A = sparse.diags(diags, [0, 1, -1, n, -n], shape=(N, N), format='lil')
    for i in range(1, n):
        A[i * n - 1, i * n] = 0.0
        A[i * n, i * n - 1] = 0.0
    A_csc = A.tocsc()

    rows, cols, vals = [], [], []
    for k in range(A_csc.shape[1]):
        start, end = A_csc.indptr[k], A_csc.indptr[k + 1]
        for p in range(start, end):
            rows.append(int(A_csc.indices[p]))
            cols.append(int(k))
            vals.append(float(A_csc.data[p]))

    b = np.ones(N, dtype=np.float64)
    x_exact = splu(A_csc).solve(b)

    np.savez(path,
             n=np.int64(n), N=np.int64(N),
             rows=np.array(rows, dtype=np.int64),
             cols=np.array(cols, dtype=np.int64),
             vals=np.array(vals, dtype=np.float64),
             b=b, x_exact=x_exact)
    INFO(f"[MATRIX] Generated n={n} N={N} nnz={len(vals)} → {path}")


def _load_matrix(path):
    """Load matrix data from .npz file. Returns dict with n, N, rows, cols, vals, b, x_exact."""
    import numpy as np
    data = np.load(path, allow_pickle=False)
    return {
        "n": int(data["n"]),
        "N": int(data["N"]),
        "rows": data["rows"].tolist(),
        "cols": data["cols"].tolist(),
        "vals": data["vals"].tolist(),
        "b": data["b"].tolist(),
        "x_exact": data["x_exact"].tolist(),
    }


def _get_matrix_data(matrix_path):
    """Load matrix data with process-local caching."""
    from fly import get_cache, has_cache, put_cache
    cache_key = f"__rasg__matrix_{matrix_path}"
    if not has_cache(cache_key):
        data = _load_matrix(matrix_path)
        put_cache(cache_key, data)
    return get_cache(cache_key)


# ── Geometry helpers ─────────────────────────────────────────────

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
    deps = [db.get_full_name("__rasg__coord")]
    if step > 0:
        use_coarse = _is_coarse(db)
        for n in neighbor_ids:
            if use_coarse:
                deps.append(db.get_full_name(f"__rasg__xc_{n}_{step - 1}"))
            else:
                deps.append(db.get_full_name(f"__rasg__x_{n}_{step - 1}"))
        if _is_adaptive(db):
            deps.append(db.get_full_name(f"__rasg__gomega_{step}"))
    return deps


def _check_deps(db, step, nsd):
    deps = [db.get_full_name(f"__rasg__conv_{i}_{step}") for i in range(nsd)]
    if step > 0 and _is_adaptive(db):
        for i in range(nsd):
            deps.append(db.get_full_name(f"__rasg__err_{i}_{step}"))
    return deps


def _aitken_omega(delta_curr, delta_prev):
    numer = sum(c * c for c in delta_curr)
    diff = [c - p for c, p in zip(delta_curr, delta_prev)]
    denom = sum(c * d for c, d in zip(delta_curr, diff))
    if abs(denom) < 1e-30:
        return 1.0
    omega = numer / denom
    return max(0.3, min(2.0, omega))


# ── Phase 1: Coordination (master process, fast — geometry only) ─

def ras_graph_coord(db, matrix_path, nsd, overlap_ratio,
                    max_iter, tol, omega=1.0):
    t_coord_start = time.perf_counter()

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

    t_coord_total = time.perf_counter() - t_coord_start
    INFO(f"[RASG COORD] n={n} nsd={nsd} ({nsd_x}x{nsd_y}) "
         f"depth={depth} overlap_ratio={overlap_ratio:.0%} "
         f"t_coord={t_coord_total*1000:.0f}ms")
    INFO(f"[RASG START] nsd={nsd} omega={omega} launching iteration loop")

    # Pre-build coarse grid on all workers before iteration loop.
    if omega == "coarse":
        INFO("[RASG] Pre-building coarse grid on all workers...")
        _prebuild_coarse_grid(db, nsd)
        INFO("[RASG] Coarse grid pre-build dispatched")

    for sd_id in range(nsd):
        ras_graph_compute(db, sd_id, 0, nsd, neighbor_ids_all[sd_id])
    ras_graph_check(db, 0, nsd, max_iter, tol, neighbor_ids_all)


# ── Coarse Grid Correction ──────────────────────────────────────

@as_task(inputs=lambda db: [db.get_full_name("__rasg__coord")])
def _prebuild_coarse_task(db):
    """Build coarse grid on a worker (dispatched to all workers)."""
    _ensure_coarse_cached(db)


def _prebuild_coarse_grid(db, nsd):
    """Dispatch coarse grid build to all workers in parallel."""
    for sd_id in range(nsd):
        _prebuild_coarse_task(db)


def _ensure_coarse_cached(db):
    from fly import get_cache, has_cache, put_cache
    if has_cache("__rasg__coarse_lu"):
        return

    INFO("[COARSE] building coarse grid...")

    coord = db.read_object("__rasg__coord")
    N = coord["N"]
    n = coord["n"]
    INFO(f"[COARSE] coord read: N={N} n={n}")
    matrix_path = coord["matrix_path"]

    data = _get_matrix_data(matrix_path)
    rows = data["rows"]
    cols = data["cols"]
    vals = data["vals"]

    stride = max(2, n // 125)
    nc = n // stride
    Nc = nc * nc

    if Nc < 10:
        INFO(f"[RASG COARSE] Skipping: coarse grid too small (nc={nc})")
        return

    t0 = time.perf_counter()
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
    t_P = time.perf_counter() - t0

    t0 = time.perf_counter()
    A_fine = sparse.csr_matrix((vals, (rows, cols)), shape=(N, N))
    t_A = time.perf_counter() - t0

    t0 = time.perf_counter()
    Ac = (P.T @ (A_fine @ P)).tocsc()
    t_Galerkin = time.perf_counter() - t0

    t0 = time.perf_counter()
    Ac_lu = splu(Ac)
    t_LU = time.perf_counter() - t0

    put_cache("__rasg__coarse_lu", Ac_lu)
    put_cache("__rasg__coarse_P", P)
    put_cache("__rasg__coarse_A", A_fine)
    put_cache("__rasg__coarse_b", np.array(data["b"], dtype=np.float64))
    put_cache("__rasg__coarse_stride", stride)
    INFO(f"[RASG COARSE] Built on worker: stride={stride} nc={nc} "
         f"Nc={Nc} nnz={Ac.nnz} t_P={t_P*1000:.0f}ms t_A={t_A*1000:.0f}ms "
         f"t_Galerkin={t_Galerkin*1000:.0f}ms t_LU={t_LU*1000:.0f}ms")


def _apply_coarse_correction(db, step, nsd):
    from fly import get_cache

    t_coarse_start = time.perf_counter()
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

    t_assemble = time.perf_counter()
    x_global = np.zeros(N, dtype=np.float64)
    for sd_id in range(nsd):
        x_sd = db.read_object(f"__rasg__x_{sd_id}_{step}")
        for pos, gidx in enumerate(primary_sets[sd_id]):
            x_global[gidx] = x_sd[pos]
    t_assemble = time.perf_counter() - t_assemble

    t_residual = time.perf_counter()
    r = b_fine - A_fine.dot(x_global)
    r_norm = np.linalg.norm(r)
    t_residual = time.perf_counter() - t_residual

    t_solve = time.perf_counter()
    e_c = Ac_lu.solve(P.T.dot(r))
    e_fine = P.dot(e_c)
    e_norm = np.linalg.norm(e_fine)
    t_solve = time.perf_counter() - t_solve

    t_write = time.perf_counter()
    for sd_id in range(nsd):
        x_sd = db.read_object(f"__rasg__x_{sd_id}_{step}")
        corrected = list(x_sd)
        for pos, gidx in enumerate(primary_sets[sd_id]):
            corrected[pos] += e_fine[gidx]
        db.write_object(f"__rasg__xc_{sd_id}_{step}",
                        np.array(corrected, dtype=np.float64), save_to_db=False)
    t_write = time.perf_counter() - t_write

    t_total = time.perf_counter() - t_coarse_start
    INFO(f"[RASG COARSE] step={step} |r|={r_norm:.2e} |e|={e_norm:.2e} "
         f"t_total={t_total*1000:.0f}ms assemble={t_assemble*1000:.0f}ms "
         f"residual={t_residual*1000:.0f}ms solve={t_solve*1000:.0f}ms "
         f"write={t_write*1000:.0f}ms")


# ── Compute (dispatched to workers via @as_task) ────────────────

@as_task(inputs=lambda db, sd_id, step, nsd, neighbor_ids:
         _compute_deps(db, sd_id, step, neighbor_ids),
         requires=lambda db, sd_id, step, nsd, neighbor_ids:
         [f"sd_{sd_id}"])
def ras_graph_compute(db, sd_id, step, nsd, neighbor_ids):
    from _fly_solver import (EXSlvSubdomainSolver, ex_slv_ras_bupdated_solve,
                              ex_slv_graph_expand_overlap,
                              ex_slv_extract_subdomain_matrix,
                              ex_slv_find_outside_connections)
    from fly import get_cache, put_cache, has_cache

    t_compute_start = time.perf_counter()

    coord = db.read_object("__rasg__coord")
    matrix_path = coord["matrix_path"]
    N = coord["N"]
    depth = coord["depth"]
    overlap_ratio = coord["overlap_ratio"]
    primary_nodes = coord["primary_sets"][sd_id]
    global_owner = coord["global_owner"]
    all_primary_sets = coord["primary_sets"]

    # ── Build setup_data (was done in coord, now done on worker) ──
    setup_key = f"__rasg__setup_{sd_id}"
    t_expand = None
    t_extract = None
    if not has_cache(setup_key):
        data = _get_matrix_data(matrix_path)
        rows = data["rows"]
        cols = data["cols"]
        vals = data["vals"]
        b = data["b"]

        primary_size = len(primary_nodes)

        t_expand = time.perf_counter()
        local_idx = ex_slv_graph_expand_overlap(
            N, rows, cols, vals, primary_nodes, depth)
        ratio = len(local_idx) / primary_size
        if ratio < 1 + overlap_ratio:
            local_idx = ex_slv_graph_expand_overlap(
                N, rows, cols, vals, primary_nodes, depth * 2)
            ratio = len(local_idx) / primary_size
        t_expand = time.perf_counter() - t_expand

        INFO(f"[RASG EXPAND] sd={sd_id} primary={primary_size} "
             f"extended={len(local_idx)} ratio={ratio:.2f}x depth={depth} "
             f"t={t_expand*1000:.0f}ms")

        local_idx_map = {gidx: pos for pos, gidx in enumerate(local_idx)}
        primary_local_pos = [local_idx_map[gidx] for gidx in primary_nodes]

        t_extract = time.perf_counter()
        size, _, a_rows, a_cols, a_vals = ex_slv_extract_subdomain_matrix(
            N, rows, cols, vals, local_idx)
        out_pos, out_gidx, out_coeffs = ex_slv_find_outside_connections(
            N, rows, cols, vals, local_idx)
        t_extract = time.perf_counter() - t_extract

        b_local = [b[gidx] for gidx in local_idx]

        neighbor_needed = {}
        for i, gidx in enumerate(out_gidx):
            owner = global_owner.get(gidx, -1)
            if owner >= 0 and owner != sd_id:
                if owner not in neighbor_needed:
                    neighbor_needed[owner] = []
                neighbor_needed[owner].append(i)

        actual_neighbor_ids = sorted(neighbor_needed.keys())

        neighbor_recv_idx = {}
        for nb_id in actual_neighbor_ids:
            nb_primary_map = {gidx: pos for pos, gidx in enumerate(all_primary_sets[nb_id])}
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
            "neighbor_ids": actual_neighbor_ids,
            "neighbor_recv_idx": neighbor_recv_idx,
            "neighbor_needed": neighbor_needed,
            "a_rows": np.array(a_rows, dtype=np.int64),
            "a_cols": np.array(a_cols, dtype=np.int64),
            "a_vals": np.array(a_vals, dtype=np.float64),
            "size": size,
        }
        put_cache(setup_key, setup_data)
        INFO(f"[RASG SETUP] sd={sd_id} t_expand={t_expand*1000:.0f}ms "
             f"t_extract={t_extract*1000:.0f}ms")

    setup = get_cache(setup_key)

    # ── Build solver (cached) ──
    solver_key = f"__rasg__solver_{sd_id}"
    t_solver_build = None
    if not has_cache(solver_key):
        t0 = time.perf_counter()
        solver = EXSlvSubdomainSolver.from_coo(
            setup["size"],
            setup["a_rows"].tolist(),
            setup["a_cols"].tolist(),
            setup["a_vals"].tolist())
        t_solver_build = time.perf_counter() - t0
        put_cache(solver_key, solver)
    solver = get_cache(solver_key)

    # ── Load tol/omega config ──
    tol_key = "__rasg__tol"
    omega_key = "__rasg__omega"
    if not has_cache(tol_key):
        cfg = db.read_object("__rasg__cfg")
        put_cache(tol_key, cfg["tol"])
        put_cache(omega_key, cfg.get("omega", 1.0))
    tol = get_cache(tol_key)
    omega_strategy = get_cache(omega_key)

    # ── Read neighbor values ──
    outside_coeffs = setup["outside_coeffs"].tolist()
    neighbor_values = [0.0] * len(outside_coeffs)
    use_coarse = _is_coarse(db)

    t_read_neighbors = None
    if step > 0:
        t_read_neighbors = time.perf_counter()
        x_prefix = "__rasg__xc_" if use_coarse else "__rasg__x_"
        for nb_id in setup["neighbor_ids"]:
            nb_x = db.read_object(f"{x_prefix}{nb_id}_{step - 1}")
            recv_positions = setup["neighbor_recv_idx"][nb_id]
            conn_indices = setup["neighbor_needed"][nb_id]
            for i, conn_i in enumerate(conn_indices):
                pos = recv_positions[i]
                if pos >= 0:
                    neighbor_values[conn_i] = nb_x[pos]
        t_read_neighbors = time.perf_counter() - t_read_neighbors

    # ── Omega computation ──
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

    # ── RAS solve ──
    t_solve_start = time.perf_counter()
    x_local = ex_slv_ras_bupdated_solve(
        solver, setup["b_orig"].tolist(),
        setup["outside_local_pos"].tolist(), outside_coeffs,
        neighbor_values, 1.0)
    t_solve = time.perf_counter() - t_solve_start

    primary_local_pos = setup["primary_local_pos"].tolist()
    x_primary = [x_local[pos] for pos in primary_local_pos]

    if step > 0 and omega != 1.0 and has_cache(prev_x_key):
        prev_x = get_cache(prev_x_key)
        x_primary = [(1.0 - omega) * p + omega * n
                     for p, n in zip(prev_x, x_primary)]

    # ── Convergence check ──
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

    # ── Write results ──
    import numpy as np
    t_write_start = time.perf_counter()
    db.write_object(f"__rasg__x_{sd_id}_{step}",
                    np.array(x_primary, dtype=np.float64), save_to_db=False)
    db.write_object(f"__rasg__conv_{sd_id}_{step}", converged_local,
                    save_to_db=False)
    if step > 0 and omega_strategy == "adaptive":
        db.write_object(f"__rasg__err_{sd_id}_{step}", max_delta,
                        save_to_db=False)
    t_write = time.perf_counter() - t_write_start

    # ── Cleanup old data ──
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

    # ── Timing log ──
    t_total = time.perf_counter() - t_compute_start
    parts = [f"[RASG COMPUTE] sd={sd_id} step={step} omega={omega:.4f} "
             f"conv_local={converged_local} t_total={t_total*1000:.0f}ms"]
    if t_expand is not None:
        parts.append(f" expand={t_expand*1000:.0f}ms")
    if t_extract is not None:
        parts.append(f" extract={t_extract*1000:.0f}ms")
    if t_solver_build is not None:
        parts.append(f" solver_build={t_solver_build*1000:.0f}ms")
    if t_read_neighbors is not None:
        parts.append(f" read_nb={t_read_neighbors*1000:.0f}ms")
    parts.append(f" solve={t_solve*1000:.0f}ms write={t_write*1000:.0f}ms")
    INFO("".join(parts))


# ── Check ───────────────────────────────────────────────────────

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
    if step > 0:
        for i in range(nsd):
            try:
                db.remove_object(f"__rasg__conv_{i}_{step - 1}")
            except Exception:
                pass
        if _is_adaptive(db):
            for i in range(nsd):
                if step > 1:
                    try:
                        db.remove_object(f"__rasg__err_{i}_{step - 1}")
                    except Exception:
                        pass
            if step > 2:
                try:
                    db.remove_object(f"__rasg__gomega_{step - 2}")
                except Exception:
                    pass
                try:
                    db.remove_object(f"__rasg__prev_err_{step - 3}")
                except Exception:
                    pass

    conv_flags = [db.read_object(f"__rasg__conv_{i}_{step}") for i in range(nsd)]
    all_converged = all(conv_flags)

    cfg = db.read_object("__rasg__cfg")
    omega_strategy = cfg.get("omega", 1.0)
    use_coarse = _is_coarse(db)

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


# ── Assemble ─────────────────────────────────────────────────────

@as_task(inputs=lambda db, nsd, final_step: [
    db.get_full_name(f"__rasg__x_{i}_{final_step}") for i in range(nsd)
])
def ras_graph_assemble(db, nsd, final_step):
    cfg = db.read_object("__rasg__cfg")
    N = cfg["N"]
    primary_sets = cfg["primary_sets"]
    use_coarse = _is_coarse(db)

    x = [0.0] * N
    for sd_id in range(nsd):
        if use_coarse:
            xc_name = db.get_full_name(f"__rasg__xc_{sd_id}_{final_step}")
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

    # Cleanup remaining temp data from the final iteration.
    for i in range(nsd):
        for prefix in [f"__rasg__x_{i}_{final_step}", f"__rasg__conv_{i}_{final_step}",
                        f"__rasg__x_{i}_{final_step - 1}", f"__rasg__conv_{i}_{final_step - 1}"]:
            try:
                db.remove_object(prefix)
            except Exception:
                pass

    INFO(f"[RASG ASSEMBLE] nsd={nsd} final_step={final_step} cleanup done")


# ── Public API ────────────────────────────────────────────────────

@wait_obj(inputs=lambda db: [db.get_full_name("__rasg__sol")])
def get_ras_graph_solution(db, timeout=3600):
    return {
        "x": db.read_object("__rasg__sol"),
        "iters": db.read_object("__rasg__iters"),
        "converged": db.read_object("__rasg__converged"),
    }


def solve_ras_graph(db, matrix_path, nsd,
                    overlap_ratio=0.50, max_iter=100, tol=1e-8,
                    omega=1.0, max_concurrent_compute=None):
    """Solve a sparse linear system using distributed RAS with graph-based overlap.

    Args:
        db: Database instance
        matrix_path: Path to .npz matrix file (generated by generate_poisson_matrix)
        nsd: Number of subdomains
        overlap_ratio: Overlap ratio (default 0.50)
        max_iter: Maximum iterations (default 100)
        tol: Convergence tolerance (default 1e-8)
        omega: Relaxation strategy. 1.0 (default), "coarse" for two-level correction,
               "adaptive" for adaptive omega
        max_concurrent_compute: Max workers running compute tasks simultaneously.
               Fewer workers = less memory (each loads full matrix).
               None means min(nsd, available_cores).
    """
    from fly.runtime import get_agent

    n_workers = min(nsd, max_concurrent_compute) if max_concurrent_compute else nsd

    master = get_agent()
    if not master.is_running() or master.worker_count < n_workers:
        worker_configs = []
        for w in range(n_workers):
            assigned = [f"sd_{s}" for s in range(nsd) if s % n_workers == w]
            worker_configs.append({"attributes": assigned})
        master.launch_local_workers(worker_configs)
        assert master.wait_for_workers(n_workers), f"{n_workers} workers should connect"

    INFO(f"[RASG WORKERS] nsd={nsd} n_workers={n_workers} "
         f"(max_concurrent_compute={max_concurrent_compute})")

    ras_graph_coord(db, matrix_path, nsd,
                    overlap_ratio, max_iter, tol, omega)
    return get_ras_graph_solution(db)
