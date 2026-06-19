"""Shared helper for golden solver accuracy tests.

Each test_golden_*.py calls run_golden(n_side, nsd, overlap, ...) which:
1. Generates a Poisson matrix with known exact solution
2. Solves it with the distributed RAS solver
3. Asserts convergence and accuracy against scipy SPLU
"""
import os
import shutil

import numpy as np
from scipy import sparse
from _fly_log import INFO


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph, generate_poisson_matrix


def run_golden(n_side, nsd, overlap_ratio=0.30, max_iter=200, tol=1e-8, omega=1.0):
    label = f"n={n_side} nsd={nsd} ratio={overlap_ratio:.0%} omega={omega}"
    db_path = f"/tmp/fly_golden_n{n_side}_sd{nsd}_r{int(overlap_ratio*100)}_o{str(omega).replace('.','_')}"
    # Use unique matrix path per process to avoid concurrent read/write conflicts
    matrix_path = f"/tmp/fly_golden_matrix_n{n_side}_{os.getpid()}.npz"

    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    get_config().set_int("fail_unscheduleable_tasks", 1)

    generate_poisson_matrix(n_side, matrix_path)
    golden = np.load(matrix_path, allow_pickle=False)
    x_exact = golden["x_exact"]
    rows, cols, vals = golden["rows"], golden["cols"], golden["vals"]
    b = golden["b"]
    N_val = int(golden["N"])
    A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))

    db = open_db(db_path)
    sol = solve_ras_graph(db, matrix_path, nsd,
                          overlap_ratio=overlap_ratio, max_iter=max_iter, tol=tol,
                          omega=omega)

    x_ras = np.array(sol["x"])
    iters = sol["iters"]
    converged = sol["converged"]

    rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
    max_error = np.max(np.abs(x_ras - x_exact))
    rel_res = np.linalg.norm(b - A_sp @ x_ras) / np.linalg.norm(b)

    status = "PASS" if converged and rel_error < 1e-4 else "FAIL"
    INFO(f"[{status}] {label}: iters={iters} converged={converged} "
         f"rel_err={rel_error:.2e} max_err={max_error:.2e} rel_res={rel_res:.2e}")

    get_agent().stop()
    assert converged, f"{label}: did not converge"
    assert rel_error < 1e-4, f"{label}: rel_error={rel_error:.2e}"
    assert rel_res < 1e-4, f"{label}: rel_res={rel_res:.2e}"
    INFO(f"[PASS] {label}")
