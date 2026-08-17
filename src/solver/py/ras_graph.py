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
from fly import register_message_id, message
from _fly_log import DBG, INFO
import math
import time
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

# 注册 solver domain 的流程性 message id（模块加载时注册）。
# SOLVER::0001: RAS 求解进度（每 10 轮迭代汇报收敛状态，收敛/结束时汇报最终结果）。
register_message_id("SOLVER::0001", "INFO")


# ── Matrix File I/O ──────────────────────────────────────────────

# 矩阵作为分布式对象入库时的约定对象名（solve_ras_graph 的 matrix_ref 对象
# 模式；golden/测试链使用，2026-08-17 矩阵入库改造）。
MATRIX_OBJ_KEY = "__rasg__matrix"


def generate_poisson_matrix(n, path, compute_exact=True):
    """Generate a Poisson 2D matrix and save to .npz file.

    Creates a 5-point stencil Laplacian on an n×n grid.
    RHS b = [1.0] * N. Golden solution computed via scipy.sparse.linalg.splu.

    Args:
        n: Grid side length (matrix is n² × n²)
        path: Output .npz file path
        compute_exact: If True (default), compute and store the exact solution
            x_exact via splu (~1.4s for n=500). If False, skip it — the exact
            solution is only used for post-solve accuracy verification, never
            by the solver itself, so callers that verify accuracy can compute
            it in parallel with the solve (see golden_solver.run_golden) to
            hide the splu cost on the critical path.
    """
    import numpy as np
    from scipy import sparse
    from scipy.sparse.linalg import splu

    N = n * n
    # Build the 5-point stencil directly as COO. Going through LIL + per-element
    # zeroing of the in-row boundary entries (the ±1 off-diagonals that would
    # otherwise wrap across grid rows) is O(n) Python loops; constructing the
    # correct sparsity pattern up front with vectorised COO is ~20x faster and
    # produces a numerically identical matrix.
    diag_idx = np.arange(N)
    # ±1 off-diagonals exist only within a grid row: (i,i+1) for non-last-column
    # rows, (i,i-1) for non-first-column rows. Masking by column-in-row position
    # drops exactly the wrap-around entries LIL zeroing removed.
    mask_r = (diag_idx + 1) % n != 0   # row i not at row-end → has right neighbour
    mask_l = diag_idx % n != 0          # row i not at row-start → has left neighbour
    r_right = diag_idx[mask_r]
    r_left = diag_idx[mask_l]
    r_down = diag_idx[:N - n]           # ±n off-diagonals: bounded by grid edge
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

    # Vectorised COO→(rows, cols, vals) extraction. The previous per-column
    # Python loop over CSC indptr/indices was O(nnz) Python iterations; numpy
    # indexing is ~70x faster for nnz~1.25M (n=500) and numerically identical.
    col_idx = np.repeat(np.arange(N), np.diff(A_csc.indptr))
    rows = A_csc.indices.astype(np.int64)
    cols = col_idx.astype(np.int64)
    vals = A_csc.data.astype(np.float64)

    b = np.ones(N, dtype=np.float64)
    # 原子写（tmp + os.replace）：与 compute_exact_solution 的重写同策略——
    # 首写当前与读方 happens-before 安全（solve 在 generate 返回后），此处
    # 为统一写协议的零成本防御（P3-24）。
    import os
    tmp_path = path + ".tmp_gen"
    with open(tmp_path, "wb") as f:
        np.savez(f,
                 n=np.int64(n), N=np.int64(N),
                 rows=rows, cols=cols, vals=vals,
                 b=b)
    os.replace(tmp_path, path)
    if compute_exact:
        x_exact = splu(A_csc).solve(b)
        # Re-save with x_exact appended. (np.savez has no append; rewrite.)
        tmp_path = path + ".tmp_exact"
        with open(tmp_path, "wb") as f:
            np.savez(f,
                     n=np.int64(n), N=np.int64(N),
                     rows=rows, cols=cols, vals=vals,
                     b=b, x_exact=x_exact)
        os.replace(tmp_path, path)
    INFO(f"[MATRIX] Generated n={n} N={N} nnz={len(vals)} "
         f"exact={'yes' if compute_exact else 'no'} → {path}")


def compute_exact_from_matrix(data):
    """内存版精确解：从矩阵 dict 直接 splu 求解，返回 x_exact（无文件 IO）。

    golden 验证链使用（2026-08-17 矩阵入库改造）：矩阵作为 DB 对象/内存 dict
    流转后，验证侧不再需要经文件的 compute_exact_solution。
    """
    import numpy as np
    from scipy import sparse
    from scipy.sparse.linalg import splu
    N = int(data["N"])
    A_csc = sparse.csc_matrix(
        (data["vals"], (data["rows"], data["cols"])), shape=(N, N))
    return splu(A_csc).solve(data["b"])


def compute_exact_solution(n, path):
    """Compute x_exact via splu and append it to an existing matrix .npz.

    Used to compute the golden solution in parallel with the distributed solve
    (it is only needed for post-solve accuracy verification, not by the solver).
    Idempotent: re-saves the file with x_exact added.

    原子写（P3-24 修复）：savez 到临时文件后 os.replace 原子替换。此前原地
    np.savez(path) 会先 truncate 再写——高负载下（coverage 全量 -j6 实测
    2/2 复现）并行的 worker task _load_matrix 读到截断视图 → zipfile
    EOFError（vals 在文件尾段最易被截）。原子替换保证读方要么见旧版完整
    文件、要么见新版，永无中间态。
    """
    import numpy as np
    import os
    from scipy import sparse
    from scipy.sparse.linalg import splu
    data = np.load(path, allow_pickle=False)
    N = int(data["N"])
    A_csc = sparse.csc_matrix(
        (data["vals"], (data["rows"], data["cols"])), shape=(N, N))
    x_exact = splu(A_csc).solve(data["b"])
    tmp_path = path + ".tmp_exact"
    with open(tmp_path, "wb") as f:
        np.savez(f,
                 n=data["n"], N=data["N"],
                 rows=data["rows"], cols=data["cols"], vals=data["vals"],
                 b=data["b"], x_exact=x_exact)
    os.replace(tmp_path, path)
    return x_exact


def _load_matrix(ref, db=None):
    """Load matrix data. Returns dict with n, N, rows, cols, vals, b, x_exact.

    双模式（2026-08-17 矩阵数据入库改造）：
      - 对象模式：db 非空 → ref 为 DB 对象名，read_object 读取（矩阵作为
        分布式数据由框架管理，worker 经正常读写路径获取——写完才可见的
        框架语义天然消除共享文件的读写时序问题）。
      - 路径模式：db 为空 → ref 为 .npz 文件路径（本地实验脚本用，不经
        分布式管理）。

    Values stay as numpy arrays (no .tolist()) — downstream consumers (the
    ex_slv_* C++ helpers and scipy sparse constructors) accept arrays directly,
    and nanobind converts a contiguous int64/float64 array to std::vector far
    faster than iterating a Python list. For n=500 (nnz~1.25M) this saves
    ~140ms per load; the matrix is loaded once per worker process.
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
    # x_exact is optional — generate_poisson_matrix(compute_exact=False) omits
    # it, since workers never use it (only post-solve verification does).
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
    # omega 模式每次从 DB 读取（cfg 是小对象，经 ObjectCache 实为内存命中）。
    # 不可缓存：worker 进程常驻，会跨多个 solve（不同 omega）复用，
    # 模块级缓存会泄漏上一次 solve 的 omega 判断。
    try:
        cfg = db.read_object("__rasg__cfg")
        omega = cfg.get("omega", 1.0)
        return isinstance(omega, str) and "coarse" in omega
    except Exception:
        return False


def _compute_deps(db, sd_id, step, neighbor_ids):
    deps = [db.get_full_name("__rasg__coord"),
            db.get_full_name(f"__rasg__setup_ready_{sd_id}")]
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

def ras_graph_coord(db, matrix_ref, nsd, overlap_ratio,
                    max_iter, tol, omega=1.0):
    t_coord_start = time.perf_counter()

    data = _load_matrix(matrix_ref, db)
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
        "matrix_ref": matrix_ref,
    }
    db.write_object("__rasg__coord", coord, save_to_db=False)

    cfg = {
        "nsd": nsd, "N": N, "n": n,
        "max_iter": max_iter, "tol": tol,
        "omega": omega,
        "primary_sets": primary_sets,
        "neighbor_ids_all": neighbor_ids_all,
        "matrix_ref": matrix_ref,
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
        # Build P + Galerkin Ac ONCE here (coord) and publish to DB, so workers
        # skip the redundant full A_fine rebuild (220ms x4) + Galerkin and only
        # do the LU step. Helps most under -j4 CPU contention.
        _prebuild_coarse_in_coord(db, n, N, matrix_ref)
        _prebuild_coarse_grid(db, nsd)
        INFO("[RASG] Coarse grid pre-build dispatched")

    for sd_id in range(nsd):
        ras_graph_setup(db, sd_id, nsd, neighbor_ids_all[sd_id])

    for sd_id in range(nsd):
        ras_graph_compute(db, sd_id, 0, nsd, neighbor_ids_all[sd_id])
    ras_graph_check(db, 0, nsd, max_iter, tol, neighbor_ids_all)


# ── Coarse Grid Correction ──────────────────────────────────────

@as_task(inputs=lambda db: [db.get_full_name("__rasg__coord")])
def _prebuild_coarse_task(db):
    """Build coarse grid on a worker (dispatched to all workers)."""
    _ensure_coarse_cached(db)


def _compute_coarse_arrays(n, N, rows, cols, vals):
    """Shared bilinear interpolation + Galerkin projection for the coarse grid.

    Builds the restriction operator P (fine → coarse via bilinear interpolation)
    and the Galerkin coarse operator Ac = P^T A_fine P. Returns None when the
    coarse grid is too small to be useful.

    Used by both _build_coarse_operators (coord prebuild, returns serialisable
    raw data) and the legacy worker-side fallback in _ensure_coarse_cached.
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
    workers can rebuild the sparse objects and do only the LU step, instead of
    each worker redundantly rebuilding the full A_fine (220ms) + Galerkin.
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


def _prebuild_coarse_grid(db, nsd):
    """Dispatch coarse grid build to all workers in parallel."""
    for sd_id in range(nsd):
        _prebuild_coarse_task(db)


def _ensure_coarse_cached(db):
    from fly import get_cache, has_cache, put_cache
    if has_cache("__rasg__coarse_lu"):
        return

    # Fast path: coord pre-built P + Galerkin Ac and published the raw sparse
    # data to DB. Rebuild the sparse objects from those arrays and do only the
    # LU step, skipping the redundant per-worker full A_fine rebuild + Galerkin
    # (was 220ms + 22ms per worker; now ~55ms LU only).
    try:
        prebuilt = db.read_object("__rasg__coarse_prebuilt")
    except Exception:
        prebuilt = None

    if prebuilt is not None:
        if prebuilt.get("skip"):
            INFO("[RASG COARSE] Skipping: coarse grid too small (coord)")
            return
        if "Ac_indptr" in prebuilt:
            from scipy import sparse
            from scipy.sparse.linalg import splu
            t0 = time.perf_counter()
            P = sparse.csr_matrix(
                (prebuilt["P_vals"], (prebuilt["P_rows"], prebuilt["P_cols"])),
                shape=(prebuilt["N"], prebuilt["Nc"]))
            Ac = sparse.csc_matrix(
                (prebuilt["Ac_data"], prebuilt["Ac_indices"],
                 prebuilt["Ac_indptr"]),
                shape=prebuilt["Ac_shape"])
            Ac_lu = splu(Ac)
            put_cache("__rasg__coarse_lu", Ac_lu)
            put_cache("__rasg__coarse_P", P)
            put_cache("__rasg__coarse_b", prebuilt["b"])
            put_cache("__rasg__coarse_stride", prebuilt["stride"])
            INFO("[RASG COARSE] Built from coord prebuilt (LU only): "
                 f"Nc={prebuilt['Nc']} t={(time.perf_counter()-t0)*1000:.0f}ms")
            return

    # Legacy fallback: coord did not prebuild (e.g. older coord path) — build
    # everything on this worker.
    INFO("[COARSE] building coarse grid (legacy path)...")
    from scipy.sparse.linalg import splu

    coord = db.read_object("__rasg__coord")
    N = coord["N"]
    n = coord["n"]
    matrix_ref = coord["matrix_ref"]

    data = _get_matrix_data(matrix_ref, db)
    rows = data["rows"]
    cols = data["cols"]
    vals = data["vals"]

    result = _compute_coarse_arrays(n, N, rows, cols, vals)
    if result is None:
        INFO("[RASG COARSE] Skipping: coarse grid too small")
        return
    P, _P_rows, _P_cols, _P_vals, A_fine, Ac, stride, nc, Nc = result
    Ac_lu = splu(Ac)

    put_cache("__rasg__coarse_lu", Ac_lu)
    put_cache("__rasg__coarse_P", P)
    put_cache("__rasg__coarse_A", A_fine)
    put_cache("__rasg__coarse_b", np.array(data["b"], dtype=np.float64))
    put_cache("__rasg__coarse_stride", stride)
    INFO(f"[RASG COARSE] Built on worker (legacy): stride={stride} nc={nc} "
         f"Nc={Nc} nnz={Ac.nnz}")


def _apply_coarse_correction(db, step, nsd):
    from fly import get_cache

    t_coarse_start = time.perf_counter()
    _ensure_coarse_cached(db)

    from fly import has_cache
    if not has_cache("__rasg__coarse_lu"):
        return

    Ac_lu = get_cache("__rasg__coarse_lu")
    P = get_cache("__rasg__coarse_P")
    b_fine = get_cache("__rasg__coarse_b")
    # A_fine is needed only for the residual r = b - A x. The coord-prebuilt
    # fast path does not cache it (only one worker runs coarse correction, so
    # building it lazily here once is cheaper than 4 workers all rebuilding it).
    from fly import has_cache as _has, put_cache as _put
    if not _has("__rasg__coarse_A"):
        coord = db.read_object("__rasg__coord")
        md = _get_matrix_data(coord["matrix_ref"], db)
        _put("__rasg__coarse_A", sparse.csr_matrix(
            (np.asarray(md["vals"]), (np.asarray(md["rows"]),
             np.asarray(md["cols"]))),
            shape=(coord["N"], coord["N"])))
    A_fine = get_cache("__rasg__coarse_A")

    # cfg 含 primary_sets（大对象，与 coord 同构），首次读后缓存，后续粗校正 O(1) 复用。
    if not _has("__rasg__cfg_cache"):
        _put("__rasg__cfg_cache", db.read_object("__rasg__cfg"))
    cfg = get_cache("__rasg__cfg_cache")
    N = cfg["N"]
    primary_sets = cfg["primary_sets"]

    t_assemble = time.perf_counter()
    x_global = np.zeros(N, dtype=np.float64)
    # Cache each subdomain's x to reuse in the correction loop below — avoids a
    # second DB read of __rasg__x_* (was ~8MB redundant read/step at n=1000).
    x_sd_cache = {}
    for sd_id in range(nsd):
        x_sd = db.read_object(f"__rasg__x_{sd_id}_{step}")
        x_sd_cache[sd_id] = x_sd
        # Vectorised scatter: assign all primary nodes of this subdomain in one
        # numpy fancy-index call instead of a per-node Python loop (the loop
        # dominates coarse-correction cost over 9 iterations).
        x_global[np.asarray(primary_sets[sd_id])] = x_sd
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
        # Reuse the value read during assembly — no second DB read.
        corrected = np.array(x_sd_cache[sd_id], dtype=np.float64)
        ps = np.asarray(primary_sets[sd_id])
        corrected += e_fine[ps]
        db.write_object(f"__rasg__xc_{sd_id}_{step}",
                        corrected, save_to_db=False)
    t_write = time.perf_counter() - t_write

    t_total = time.perf_counter() - t_coarse_start
    INFO(f"[RASG COARSE] step={step} |r|={r_norm:.2e} |e|={e_norm:.2e} "
         f"t_total={t_total*1000:.0f}ms assemble={t_assemble*1000:.0f}ms "
         f"residual={t_residual*1000:.0f}ms solve={t_solve*1000:.0f}ms "
         f"write={t_write*1000:.0f}ms")


# ── Setup (separate task: matrix setup + need_map, decoupled from iteration) ──

@as_task(inputs=lambda db, sd_id, nsd, neighbor_ids:
         [db.get_full_name("__rasg__coord")],
         requires=lambda db, sd_id, nsd, neighbor_ids:
         [f"sd_{sd_id}"])
def ras_graph_setup(db, sd_id, nsd, neighbor_ids):
    """Per-subdomain setup: BFS expand, rank-filter extract, build solver.
    Decoupled from iteration so compute is purely iterative. Pinned to worker
    sd_{sd_id} (same as compute) to share process-local caches."""
    import numpy as np
    from _fly_solver import EXSlvSubdomainSolver
    from fly import get_cache, put_cache, has_cache

    setup_key = f"__rasg__setup_{sd_id}"
    if has_cache(setup_key):
        db.write_object(f"__rasg__setup_ready_{sd_id}", True, save_to_db=False)
        return

    coord = db.read_object("__rasg__coord")
    matrix_ref = coord["matrix_ref"]
    N = coord["N"]
    depth = coord["depth"]
    overlap_ratio = coord["overlap_ratio"]
    primary_nodes = coord["primary_sets"][sd_id]
    global_owner = coord["global_owner"]
    all_primary_sets = coord["primary_sets"]

    data = _get_matrix_data(matrix_ref, db)
    rows, cols, vals, b = data["rows"], data["cols"], data["vals"], data["b"]
    primary_size = len(primary_nodes)

    # BFS overlap expansion (cached adjacency index shared across subdomains).
    adj_key = f"__rasg__adj_{matrix_ref}"
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

    # Rank-array filter for subdomain matrix + outside connections.
    rows_arr = np.asarray(rows); cols_arr = np.asarray(cols); vals_arr = np.asarray(vals)
    _rank = np.full(N, -1, dtype=np.int32)
    _rank[local_idx] = np.arange(len(local_idx))
    row_rank = _rank[rows_arr]; col_rank = _rank[cols_arr]
    in_local = (row_rank >= 0) & (col_rank >= 0)
    a_rows = row_rank[in_local]; a_cols = col_rank[in_local]; a_vals = vals_arr[in_local]
    size = len(local_idx)
    is_outside = ((col_rank >= 0) & (row_rank < 0) &
                  (rows_arr != cols_arr) & (vals_arr != 0.0))
    out_pos = col_rank[is_outside]; out_gidx = rows_arr[is_outside]; out_coeffs = vals_arr[is_outside]
    b_local = b[local_idx]

    neighbor_needed = {}
    for i, g in enumerate(out_gidx):
        owner = global_owner.get(int(g), -1)
        if owner >= 0 and owner != sd_id:
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
        "need_map": need_map,
        "a_rows": np.array(a_rows, dtype=np.int64),
        "a_cols": np.array(a_cols, dtype=np.int64),
        "a_vals": np.array(a_vals, dtype=np.float64),
        "size": size,
    }
    put_cache(setup_key, setup_data)

    solver = EXSlvSubdomainSolver.from_coo(
        size, a_rows.tolist(), a_cols.tolist(), a_vals.tolist())
    put_cache(f"__rasg__solver_{sd_id}", solver)

    db.write_object(f"__rasg__need_{sd_id}", need_map, save_to_db=False)
    db.write_object(f"__rasg__setup_ready_{sd_id}", True, save_to_db=False)
    INFO(f"[RASG SETUP] sd={sd_id} primary={primary_size} "
         f"extended={len(local_idx)} ratio={ratio:.2f}x neighbors={actual_neighbor_ids}")


# ── Compute (dispatched to workers via @as_task) ────────────────

@as_task(inputs=lambda db, sd_id, step, nsd, neighbor_ids:
         _compute_deps(db, sd_id, step, neighbor_ids),
         requires=lambda db, sd_id, step, nsd, neighbor_ids:
         [f"sd_{sd_id}"])
def ras_graph_compute(db, sd_id, step, nsd, neighbor_ids):
    import numpy as np
    from _fly_solver import ex_slv_ras_bupdated_solve
    from fly import get_cache, put_cache, has_cache

    t_compute_start = time.perf_counter()

    # coord 在整个求解过程中是常量（setup 阶段写入后不变），含 primary_sets（每子域
    # ~62500 节点）+ global_owner（百万节点 dict），反序列化开销大（n=1000 实测 ~130ms）。
    # 首次读取后缓存到进程级 cache，后续迭代 O(1) 命中，消除每迭代的大对象反序列化。
    # 注意：函数体内不再使用 coord 的任何字段（setup/solver 已含全部所需数据），保留
    # 读取仅为与 _compute_deps 的依赖声明语义对称（coord 就绪由调度器保证）。
    coord_cache_key = "__rasg__coord_cache"
    if not has_cache(coord_cache_key):
        put_cache(coord_cache_key, db.read_object("__rasg__coord"))

    # Setup done by ras_graph_setup (same worker via requires=sd_{sd_id}).
    setup_key = f"__rasg__setup_{sd_id}"
    setup = get_cache(setup_key)
    solver = get_cache(f"__rasg__solver_{sd_id}")

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
    outside_coeffs = setup["outside_coeffs"]
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
    # setup arrays are already numpy (④ optimization); pass them directly to
    # the C++ helper instead of re-converting via .tolist() on every iteration
    # (was ~30ms/iter wasted on redundant Python-list construction).
    x_local = ex_slv_ras_bupdated_solve(
        solver, setup["b_orig"],
        setup["outside_local_pos"], outside_coeffs,
        neighbor_values, 1.0)
    t_solve = time.perf_counter() - t_solve_start

    # Vectorised primary extraction: gather x_local at primary positions in one
    # numpy call (was a per-element Python loop over ~15k primary nodes).
    primary_local_pos = setup["primary_local_pos"]
    x_primary = np.asarray(x_local, dtype=np.float64)[primary_local_pos]

    if step > 0 and omega != 1.0 and has_cache(prev_x_key):
        prev_x = np.asarray(get_cache(prev_x_key), dtype=np.float64)
        # Vectorised relaxation (was a per-element Python loop).
        x_primary = (1.0 - omega) * prev_x + omega * x_primary

    # ── Convergence check ──
    converged_local = False
    max_delta = 0.0
    if step > 0 and has_cache(prev_x_key):
        prev_x = np.asarray(get_cache(prev_x_key), dtype=np.float64)
        # Vectorised convergence check (was a per-element abs/max Python loop).
        max_delta = float(np.max(np.abs(x_primary - prev_x)))
        converged_local = max_delta < tol

    if has_cache(prev2_x_key):
        put_cache(prev3_x_key, get_cache(prev2_x_key))
    if has_cache(prev_x_key):
        put_cache(prev2_x_key, get_cache(prev_x_key))
    put_cache(prev_x_key, x_primary)

    # ── Write results ──
    import numpy as np
    t_write_start = time.perf_counter()
    db.write_object(f"__rasg__x_{sd_id}_{step}",
                    x_primary, save_to_db=False)
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

    # cfg 含 primary_sets（大对象），首次读后缓存，后续迭代 O(1) 复用。
    from fly import has_cache as _hc, put_cache as _pc, get_cache as _gc
    if not _hc("__rasg__cfg_cache"):
        _pc("__rasg__cfg_cache", db.read_object("__rasg__cfg"))
    cfg = _gc("__rasg__cfg_cache")
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
        db.write_object("__rasg__gomega_1", 1.5,
                        save_to_db=False)
        INFO("[RASG ADAPTIVE] step=0 gomega=1.5000 (initial)")

    DBG(f"[RASG CHECK] step={step} converged={all_converged} "
        f"flags={conv_flags}")

    # 流程性 message：每 10 轮汇报迭代进度（source=2），便于观察收敛趋势。
    if not all_converged and (step + 1) % 10 == 0 and step < max_iter - 1:
        message("SOLVER::0001", 2,
                f"RAS iterating: step={step + 1}, n={cfg['n']}, nsd={nsd}")

    if all_converged or step >= max_iter - 1:
        db.write_object("__rasg__converged", all_converged, save_to_db=False)
        db.write_object("__rasg__iters", step + 1, save_to_db=False)
        # 收敛/达到上限时汇报最终结果（source=1 标注收敛节点）。
        status = "converged" if all_converged else "maxiter reached"
        message("SOLVER::0001", 1,
                f"RAS {status}: iters={step + 1}, n={cfg['n']}, nsd={nsd}")
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
    import numpy as np
    from fly import has_cache, put_cache, get_cache

    # cfg 含 primary_sets（大对象），首次读后缓存复用（与 compute/coarse 一致）。
    if not has_cache("__rasg__cfg_cache"):
        put_cache("__rasg__cfg_cache", db.read_object("__rasg__cfg"))
    cfg = get_cache("__rasg__cfg_cache")
    N = cfg["N"]
    primary_sets = cfg["primary_sets"]
    use_coarse = _is_coarse(db)

    # numpy 向量化 scatter（替代原 Python 逐元素循环，n=1000 下 100 万次循环）。
    # 与 _apply_coarse_correction 的 assemble 阶段一致：x_global[primary_set] = x_sd。
    x_global = np.zeros(N, dtype=np.float64)
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
        x_global[np.asarray(primary_sets[sd_id])] = x_sd

    db.write_object("__rasg__sol", x_global, save_to_db=True)

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


def solve_ras_graph(db, matrix_ref, nsd,
                    overlap_ratio=0.50, max_iter=100, tol=1e-8,
                    omega=1.0, max_concurrent_compute=None):
    """Solve a sparse linear system using distributed RAS with graph-based overlap.

    Args:
        db: Database instance
        matrix_ref: 矩阵来源（双模式）。对象名（推荐，如 "__rasg__matrix"）——矩阵经
            db.write_object 入库，worker 经框架读写路径获取，数据依赖驱动调度；
            或 .npz 文件路径（仅限本地实验脚本，不经分布式管理）
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

    ras_graph_coord(db, matrix_ref, nsd,
                    overlap_ratio, max_iter, tol, omega)
    return get_ras_graph_solution(db)
