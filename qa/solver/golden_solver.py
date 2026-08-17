"""Shared helper for golden solver accuracy tests.

Each test_golden_*.py calls run_golden(n_side, nsd, overlap, ...) which:
1. Loads a Poisson matrix with known exact solution
2. Writes it into the DB as a distributed object (write_object)
3. Solves it with the distributed RAS solver
4. Asserts convergence and accuracy against the exact solution

2026-08-17 矩阵入库改造（用户裁定）：矩阵不再经 /tmp 共享 npz 文件传递——
此前 exact 后台线程原地重写文件曾造成 worker 读到截断视图（P3-24），且
绕过了框架的数据与依赖管理。现在矩阵经 db.write_object 入库、worker 经
read_object 正常路径获取（写完才可见的框架语义），exact 解在内存计算。
"""
import os
import shutil
import threading

import numpy as np
from scipy import sparse
from _fly_log import INFO


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph, generate_poisson_matrix
from solver import compute_exact_from_matrix, MATRIX_OBJ_KEY


_MATRIX_DIR = os.path.join(os.path.dirname(__file__), "matrices")


def _get_matrix(n_side):
    """Return (matrix_dict, has_exact). 全内存流转，不经共享文件。

    Prefer a pre-generated matrix file (matrices/poisson_n{n}.npz, with x_exact
    computed offline; 读入后即与文件解耦). Fall back to on-the-fly generation
    (without x_exact) via a process-private tempfile (读完即删，无共享).
    """
    import tempfile
    prebuilt = os.path.join(_MATRIX_DIR, f"poisson_n{n_side}.npz")
    if os.path.exists(prebuilt):
        data = np.load(prebuilt, allow_pickle=False)
        d = {k: data[k] for k in data.files}
        return d, "x_exact" in d
    with tempfile.TemporaryDirectory() as td:
        tmp = os.path.join(td, "m.npz")
        generate_poisson_matrix(n_side, tmp, compute_exact=False)
        data = np.load(tmp, allow_pickle=False)
        d = {k: data[k] for k in data.files}
    return d, False


def run_golden(n_side, nsd, overlap_ratio=0.30, max_iter=200, tol=1e-8, omega=1.0):
    label = f"n={n_side} nsd={nsd} ratio={overlap_ratio:.0%} omega={omega}"
    db_path = f"/tmp/fly_golden_n{n_side}_sd{nsd}_r{int(overlap_ratio*100)}_o{str(omega).replace('.','_')}"

    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    get_config().set_int("fail_unscheduleable_tasks", 1)

    # Load the matrix. Pre-generated files carry x_exact (computed offline), so
    # no splu runs at QA time at all. Only the fallback path (no pre-built file)
    # computes x_exact, overlapped with the solve for small n / serial for large.
    golden, has_exact = _get_matrix(n_side)
    b = golden["b"]
    N_val = int(golden["N"])

    exact_thread = None
    if has_exact:
        x_exact = golden["x_exact"]
    else:
        # No precomputed exact solution: compute it in-memory overlapped with
        # the solve. Only overlap for small matrices (n<=700) -- for large n
        # splu dominates and a background thread steals a core from RAS workers.
        exact_result = {}
        def _compute_exact():
            exact_result["x"] = compute_exact_from_matrix(golden)
        if n_side <= 700:
            exact_thread = threading.Thread(target=_compute_exact, daemon=True)
            exact_thread.start()
        else:
            _compute_exact()
            x_exact = exact_result["x"]

    db = open_db(db_path)
    # 矩阵入库：作为分布式对象由框架管理（数据依赖驱动调度，worker 经
    # read_object 正常路径获取——写完才可见，无共享文件时序问题）。
    db.write_object(MATRIX_OBJ_KEY, golden)
    sol = solve_ras_graph(db, MATRIX_OBJ_KEY, nsd,
                          overlap_ratio=overlap_ratio, max_iter=max_iter, tol=tol,
                          omega=omega)

    x_ras = np.array(sol["x"])
    iters = sol["iters"]
    converged = sol["converged"]

    if exact_thread is not None:
        exact_thread.join()
        x_exact = exact_result["x"]

    rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
    max_error = np.max(np.abs(x_ras - x_exact))
    # Residual ||b - A x||: build A_sp once here for verification only.
    A_sp = sparse.csc_matrix((golden["vals"], (golden["rows"], golden["cols"])),
                             shape=(N_val, N_val))
    rel_res = np.linalg.norm(b - A_sp @ x_ras) / np.linalg.norm(b)

    status = "PASS" if converged and rel_error < 1e-4 else "FAIL"
    INFO(f"[{status}] {label}: iters={iters} converged={converged} "
         f"rel_err={rel_error:.2e} max_err={max_error:.2e} rel_res={rel_res:.2e}")

    get_agent().stop()
    assert converged, f"{label}: did not converge"
    assert rel_error < 1e-4, f"{label}: rel_error={rel_error:.2e}"
    assert rel_res < 1e-4, f"{label}: rel_res={rel_res:.2e}"
    INFO(f"[PASS] {label}")
