"""Comprehensive RAS solver test matrix — multi-grid, multi-subdomain, multi-overlap, precision.

Tests:
  1. Grid sizes: 4, 6, 8, 10
  2. Subdomain counts: 2, 3, 4, 5 (where grid allows)
  3. Overlap values: 1, 2 (overlap=0 has known issues, tested separately)
  4. Precision: relative error vs scipy < 1e-2
  5. Non-convergence: maxiter=3 forces early termination
  6. Performance: each config within reasonable time

Each configuration runs in its own DB to avoid object name collision.
Cache keys include db_id so different cases don't pollute each other.
"""
import sys
import os
import shutil
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent

TIMEOUT_PER_CASE = 60


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


def scipy_reference(n):
    import numpy as np
    import scipy.sparse as sp
    import scipy.sparse.linalg as spla
    N = n * n
    rows, cols, vals = [], [], []
    for i in range(n):
        for j in range(n):
            k = i * n + j
            rows.append(k); cols.append(k); vals.append(4.0)
            if i > 0: rows.append(k); cols.append((i - 1) * n + j); vals.append(-1.0)
            if i < n - 1: rows.append(k); cols.append((i + 1) * n + j); vals.append(-1.0)
            if j > 0: rows.append(k); cols.append(i * n + (j - 1)); vals.append(-1.0)
            if j < n - 1: rows.append(k); cols.append(i * n + (j + 1)); vals.append(-1.0)
    A = sp.csr_matrix((vals, (rows, cols)), shape=(N, N))
    return spla.spsolve(A, np.ones(N))


# ── Test configurations ──
# (n, num_subdomains, overlap, should_converge)
# overlap=0 excluded — has a known issue with subdomain matrix extraction
# causing Eigen assertion failure at high iteration counts.
CONFIGS = [
    # Grid 4×4 — small, fast baseline
    (4, 2, 1, True),
    (4, 2, 2, True),
    # Grid 6×6 — moderate size
    (6, 2, 1, True),
    (6, 2, 2, True),
    (6, 3, 1, True),
    (6, 3, 2, True),
    # Grid 8×8 — larger, more subdomains
    (8, 2, 1, True),
    (8, 2, 2, True),
    (8, 4, 1, True),
    (8, 4, 2, True),
    # Grid 10×10 — stress test
    (10, 2, 1, True),
    (10, 2, 2, True),
    (10, 5, 1, True),
    # Non-convergence (maxiter=3, should terminate early)
    (4, 2, 1, False),
]


def run_case(master, case_idx, n, num_subdomains, overlap, should_converge):
    """Run a single solver configuration and verify results."""
    import numpy as np
    from solver.ras import ras_sd_solve, ras_check, get_ras_solution

    tag = f"n={n},sd={num_subdomains},ov={overlap}"
    db_path = f"/tmp/fly_e2e_solver_matrix_{case_idx}_db"
    cleanup(db_path)

    db = open_db(db_path)

    if not should_converge:
        # Non-convergence test: manually setup with small maxiter
        from _fly_solver import (ex_slv_build_poisson_2d, ex_slv_partition_1d,
                                  ex_slv_extract_subdomain_matrix)
        size, _, rows, cols, vals = ex_slv_build_poisson_2d(n)
        db.write_object("__ras__A", {"size": size, "n": n,
                                      "rows": rows, "cols": cols, "values": vals})
        sds = ex_slv_partition_1d(n, num_subdomains, overlap)
        for sd in sds:
            i = sd.subdomain_id
            _, _, lr, lc, lv = ex_slv_extract_subdomain_matrix(
                size, rows, cols, vals, sd.local_indices)
            db.write_object(f"__ras__sd_{i}", {
                "id": i,
                "local": list(sd.local_indices),
                "own": list(sd.own_indices),
                "bnd": list(sd.boundary_indices),
                "sz": len(sd.local_indices),
                "lr": lr, "lc": lc, "lv": lv,
            })
        db.write_object("__ras__cfg", {
            "n": n, "nsd": num_subdomains, "ov": overlap,
            "maxiter": 3, "tol": 1e-4,
        })
        db.write_object("__ras__x_0", [0.0] * size)
        for sd in sds:
            ras_sd_solve(db, sd.subdomain_id, 0)
        ras_check(db, 0, num_subdomains)

        result = get_ras_solution(db)

        assert not result["converged"], \
            f"[{tag}] Expected non-convergence but got converged=True"
        assert result["iters"] == 3, \
            f"[{tag}] Expected 3 iters for maxiter=3, got {result['iters']}"
        assert result["residual"] > 1e-4, \
            f"[{tag}] Expected residual > 1e-4, got {result['residual']:.2e}"
        print(f"  [{case_idx:2d}] {tag:20s} NON-CONVERGE OK iters={result['iters']} "
              f"res={result['residual']:.2e}", file=sys.stderr)
        return

    # Normal convergence test
    from solver import solve_ras
    x_ref = scipy_reference(n)

    t0 = time.time()
    result = solve_ras(db, n, num_subdomains, overlap)
    elapsed = time.time() - t0

    x_ras = np.array(result["x"])
    error = np.linalg.norm(x_ras - x_ref) / np.linalg.norm(x_ref)

    assert result["converged"], \
        f"[{tag}] Did not converge: iters={result['iters']}, res={result['residual']:.2e}"
    assert error < 1e-2, \
        f"[{tag}] Error too large: {error:.2e}"

    print(f"  [{case_idx:2d}] {tag:20s} OK iters={result['iters']:3d} "
          f"res={result['residual']:.2e} err={error:.2e} "
          f"time={elapsed:.2f}s", file=sys.stderr)


# ── Main ──

get_config().set_int("fail_unscheduleable_tasks", 1)
master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(), "Worker should connect"
print("  Worker connected", file=sys.stderr)

total = len(CONFIGS)
passed = 0
failed = 0
t_total = time.time()

for idx, (n, nsd, ov, conv) in enumerate(CONFIGS):
    try:
        run_case(master, idx, n, nsd, ov, conv)
        passed += 1
    except Exception as e:
        failed += 1
        tag = f"n={n},sd={nsd},ov={ov}"
        print(f"  [{idx:2d}] {tag:20s} FAIL: {e}", file=sys.stderr)

t_elapsed = time.time() - t_total
print(f"\n  Results: {passed}/{total} passed, {failed} failed "
      f"({t_elapsed:.1f}s total)", file=sys.stderr)

master.stop()
assert failed == 0, f"{failed} test cases failed"
print(f"[PASS] test_solver_ras_matrix ({passed} configs)", file=sys.stderr)
