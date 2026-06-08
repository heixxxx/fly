"""Test n=1000 with distributed setup. Each config as independent subprocess."""
import subprocess
import sys
import os

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FLY_BIN = os.path.join(PROJECT_ROOT, 'build', 'bin', 'fly')
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs', 'golden')
os.makedirs(LOG_DIR, exist_ok=True)

RUNNER_SCRIPT = '''"""Single n=1000 test case."""
import sys, os, shutil, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

DB_PATH = f"/tmp/fly_golden_n{N_SIDE}_sd{NSD}_r{int(OVERLAP*100)}"

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph

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

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)
master = get_agent()
master.launch_local_workers([{}] * NSD)
assert master.wait_for_workers(NSD), "workers should connect"

t0 = time.time()
N_val, rows, cols, vals, A_csc = build_poisson_2d(N_SIDE)
b = [1.0] * N_val
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))
t_build = time.time() - t0

INFO(f"Matrix build: N={N_val} nnz={A_sp.nnz} time={t_build:.1f}s")

t0 = time.time()
x_exact = splu(A_sp).solve(np.array(b))
t_exact = time.time() - t0
INFO(f"Exact solve: time={t_exact:.1f}s")

t0 = time.time()
db = open_db(DB_PATH)
sol = solve_ras_graph(db, N_val, rows, cols, vals, b, NSD,
                      overlap_ratio=OVERLAP, max_iter=MAX_ITER, tol=TOL)
t_solve = time.time() - t0

x_ras = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]

rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
max_error = np.max(np.abs(x_ras - x_exact))
rel_res = np.linalg.norm(np.array(b) - A_sp @ x_ras) / np.linalg.norm(b)

label = f"n={N_SIDE} nsd={NSD} ratio={OVERLAP:.0%}"
status = "PASS" if converged and rel_error < 1e-4 else "FAIL"
INFO(f"[{status}] {label}: iters={iters} converged={converged} "
     f"rel_err={rel_error:.2e} max_err={max_error:.2e} rel_res={rel_res:.2e} "
     f"solve={t_solve:.1f}s")

master.stop()
assert converged, f"{label}: did not converge in {iters} iters"
assert rel_error < 1e-4, f"{label}: rel_error={rel_error:.2e}"
'''

# Scaling test configs — nsd ≤ 8, higher overlap ratio to reduce iterations
CONFIGS = [
    # (n_side, nsd, overlap_ratio, max_iter, tol)
    (500, 4, 0.50, 200, 1e-8),    # n=500 nsd=4 ratio=50%
    (1000, 4, 0.50, 300, 1e-8),   # n=1000 nsd=4 ratio=50%
    (1000, 8, 0.50, 300, 1e-8),   # n=1000 nsd=8 ratio=50%
]

passed = 0
failed = 0
results = []

for n_side, nsd, overlap, max_iter, tol in CONFIGS:
    label = f"n={n_side} nsd={nsd} ratio={overlap:.0%}"
    script_path = os.path.join('/tmp', f'_golden_n{n_side}_sd{nsd}.py')

    code = f'N_SIDE={n_side}\nNSD={nsd}\nOVERLAP={overlap}\nMAX_ITER={max_iter}\nTOL={tol}\n'
    code += RUNNER_SCRIPT

    with open(script_path, 'w') as f:
        f.write(code)

    log_file = os.path.join(LOG_DIR, f'n{n_side}_sd{nsd}.log')

    print(f"  Running {label}...", end='', flush=True)
    result = subprocess.run(
        [FLY_BIN, '--log-dir', os.path.join(LOG_DIR, f'n{n_side}_sd{nsd}'), script_path],
        capture_output=True, text=True, timeout=1800, cwd=PROJECT_ROOT,
    )

    with open(log_file, 'w') as f:
        f.write(result.stdout)
        f.write('\n--- STDERR ---\n')
        f.write(result.stderr)

    if result.returncode == 0:
        passed += 1
        # Extract solve time from log
        solve_info = ""
        for line in result.stdout.split('\n'):
            if '[PASS]' in line or '[FAIL]' in line:
                solve_info = line.split('INFO] ')[-1] if 'INFO]' in line else line
                break
        results.append(f"  ✓ PASS  {label}")
        print(f" ✓  {solve_info}", flush=True)
    else:
        failed += 1
        results.append(f"  ✗ FAIL  {label}")
        print(f" ✗", flush=True)
        lines = result.stdout.strip().split('\n')
        for line in lines[-15:]:
            if any(k in line for k in ['ERROR', 'Error', 'FAIL', 'Traceback', 'Assertion']):
                print(f"    {line}", flush=True)

print(f"\n{'='*60}")
print(f"Results: {passed} passed, {failed} failed")
for r in results:
    print(r)

assert failed == 0, f"{failed} tests failed"
print(f"\n[ALL PASS] n=1000 scaling test: {len(CONFIGS)} configs")
