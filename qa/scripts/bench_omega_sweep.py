"""Sweep omega parameter for RAS solver — one config per subprocess.

Usage: ./fly.sh build && ./fly.sh install && ./build/bin/fly qa/bench_omega_sweep.py
"""
from _fly_log import INFO, ERR
import sys
import os
import shutil
import subprocess
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

FLY_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       '..', 'build', 'bin', 'fly')

N_SIDE = 50
NSD = 4
DB_PATH = "/tmp/fly_bench_omega_db"
OVERLAP_RATIO = 0.60
MAX_ITER = 100
TOL = 1e-8


def build_poisson_2d(n):
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
    return N, rows, cols, vals, A_csc


RUNNER_SCRIPT = '''"""Auto-generated omega test runner."""
from _fly_log import INFO
import sys, os, shutil, numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph

DB_PATH = "{db_path}"
OMEGA = {omega}
N_SIDE = {n_side}
NSD = {nsd}
OVERLAP_RATIO = {overlap_ratio}
MAX_ITER = {max_iter}
TOL = {tol}

def build_poisson_2d(n):
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
    return N, rows, cols, vals, A_csc

shutil.rmtree(DB_PATH, ignore_errors=True)
get_config().set_int("fail_unscheduleable_tasks", 1)

N, rows, cols, vals, A_csc = build_poisson_2d(N_SIDE)
b = [1.0] * N
x_exact = splu(A_csc).solve(np.array(b))

db = open_db(DB_PATH)
sol = solve_ras_graph(db, N, rows, cols, vals, b, NSD,
                      overlap_ratio=OVERLAP_RATIO,
                      max_iter=MAX_ITER, tol=TOL,
                      omega=OMEGA)

x = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]
rel_error = np.linalg.norm(x - x_exact) / np.linalg.norm(x_exact)

status = "PASS" if converged and rel_error < 1e-4 else "FAIL"
INFO(f"[RESULT] omega={{OMEGA}} iters={{iters}} conv={{converged}} "
     f"err={{rel_error:.2e}} status={{status}}")

from fly.runtime import get_agent
get_agent().stop()
'''

N, rows, cols, vals, A_csc = build_poisson_2d(N_SIDE)
x_exact = splu(A_csc).solve(np.ones(N))

omegas = [0.5, 0.7, 1.0, 1.2, 1.5, 1.8, "aitken", "adaptive"]

INFO(f"Omega sweep: n={N_SIDE} N={N} nsd={NSD} overlap={OVERLAP_RATIO} tol={TOL}")

results = []
tmpdir = "/tmp/fly_bench_omega"
os.makedirs(tmpdir, exist_ok=True)

for omega_val in omegas:
    label = str(omega_val)
    script_path = os.path.join(tmpdir, f"run_omega_{label}.py")
    db_path = f"/tmp/fly_bench_omega_db_{label}"

    script_content = RUNNER_SCRIPT.format(
        db_path=db_path, omega=repr(omega_val),
        n_side=N_SIDE, nsd=NSD,
        overlap_ratio=OVERLAP_RATIO, max_iter=MAX_ITER, tol=TOL)

    with open(script_path, 'w') as f:
        f.write(script_content)

    INFO(f"Running omega={label}...")
    try:
        result = subprocess.run(
            [os.path.abspath(FLY_BIN), script_path],
            capture_output=True, text=True, timeout=300,
            cwd=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

        output = result.stdout + result.stderr
        found = False
        for line in output.split('\n'):
            if '[RESULT]' in line:
                INFO(f"  {line.strip()}")
                found = True
                if 'status=PASS' in line:
                    parts = line.split('iters=')[1].split()[0]
                    iters = int(parts.rstrip(','))
                    err_str = line.split('err=')[1].split()[0] if 'err=' in line else '?'
                    results.append((label, iters, True, err_str))
                else:
                    results.append((label, -1, False, '?'))
                break
        if not found:
            ERR(f"  omega={label}: no RESULT line found (exit={result.returncode})")
            results.append((label, -1, False, '?'))
    except subprocess.TimeoutExpired:
        ERR(f"  omega={label}: TIMEOUT after 300s")
        results.append((label, -1, False, 'timeout'))
    except Exception as e:
        ERR(f"  omega={label}: {e}")
        results.append((label, -1, False, str(e)))

INFO("=" * 60)
INFO("Summary:")
best = None
for label, iters, conv, err in results:
    marker = ""
    if conv and iters > 0:
        if best is None or iters < best[1]:
            best = (label, iters)
            marker = " <-- best"
    INFO(f"  omega={label:>8s}: {iters:3d} iters  err={err}{marker}")
