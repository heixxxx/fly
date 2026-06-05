"""Test distributed RAS solver — convergence and precision vs scipy.

Baseline (single-process, n=4, 2 subdomains): 8 iters, 1.2ms.
Distributed overhead ~50ms/task × 24 tasks ≈ 1.2s.
Timeout set to 30s (25x baseline).
"""
import sys
import os
import shutil
import time

DB_PATH = "/tmp/fly_e2e_solver_ras_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent

TIMEOUT = 30  # 25x single-process baseline


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


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
            if i > 0: rows.append(k); cols.append((i-1)*n+j); vals.append(-1.0)
            if i < n-1: rows.append(k); cols.append((i+1)*n+j); vals.append(-1.0)
            if j > 0: rows.append(k); cols.append(i*n+(j-1)); vals.append(-1.0)
            if j < n-1: rows.append(k); cols.append(i*n+(j+1)); vals.append(-1.0)
    A = sp.csr_matrix((vals, (rows, cols)), shape=(N, N))
    return spla.spsolve(A, np.ones(N))


def wait_for(condition, timeout=TIMEOUT, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


# ── Main test ──

cleanup()
get_config().set_int("fail_unscheduleable_tasks", 1)

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(), "Worker should connect"
print("  Phase 1 OK: worker connected", file=sys.stderr)

N = 4; NSD = 2; OVERLAP = 1
x_ref = scipy_reference(N)
import numpy as np
print(f"  scipy reference: ||x||={np.linalg.norm(x_ref):.6f}", file=sys.stderr)

db = open_db(DB_PATH)

# Submit RAS tasks — solve_ras blocks via @wait_obj until __ras__sol is ready
from solver import solve_ras
t0 = time.time()
result = solve_ras(db, N, NSD, OVERLAP)
elapsed = time.time() - t0

print(f"  RAS: iters={result['iters']}, residual={result['residual']:.2e}, "
      f"converged={result['converged']}, time={elapsed:.2f}s", file=sys.stderr)

assert result["converged"], \
    f"RAS did not converge in {result['iters']} iters (res={result['residual']:.2e})"
print("  Phase 2 OK: RAS converged", file=sys.stderr)

x_ras = np.array(result["x"])
error = np.linalg.norm(x_ras - x_ref) / np.linalg.norm(x_ref)
print(f"  Relative error vs scipy: {error:.2e}", file=sys.stderr)
assert error < 1e-2, f"Error too large: {error:.2e}"
print("  Phase 3 OK: precision verified", file=sys.stderr)

master.stop()
print("[PASS] test_solver_ras", file=sys.stderr)
