"""Golden data accuracy verification: compare RAS solver with scipy SPLU exact solution.
Runs each config as independent subprocess with its own agent lifecycle."""
import subprocess
import sys
import os
import shutil

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FLY_BIN = os.path.join(PROJECT_ROOT, 'build', 'bin', 'fly')
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs', 'golden')

# (n_side, nsd, overlap_ratio, max_iter, tol)
CONFIGS = [
    (20, 2, 0.30, 200, 1e-8),
    (20, 4, 0.30, 200, 1e-8),
    (20, 4, 0.50, 200, 1e-8),
    (30, 4, 0.30, 200, 1e-8),
    (50, 4, 0.30, 200, 1e-8),
    (50, 4, 0.20, 200, 1e-8),
    (50, 9, 0.30, 300, 1e-8),
]

# Write the runner script that each subprocess will execute
RUNNER_SCRIPT = '''"""Single golden data test case — spawned by test_golden_accuracy.py"""
import sys, os, shutil
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

import numpy as np
from scipy import sparse
from _fly_log import INFO

DB_PATH = f"/tmp/fly_golden_n{N_SIDE}_sd{NSD}_r{int(OVERLAP*100)}"
MATRIX_PATH = f"/tmp/fly_golden_matrix_n{N_SIDE}.npz"

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph, generate_poisson_matrix

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)

generate_poisson_matrix(N_SIDE, MATRIX_PATH)
golden = np.load(MATRIX_PATH, allow_pickle=False)
x_exact = golden["x_exact"]
rows, cols, vals = golden["rows"], golden["cols"], golden["vals"]
b = golden["b"]
N_val = int(golden["N"])
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))

db = open_db(DB_PATH)
sol = solve_ras_graph(db, MATRIX_PATH, NSD,
                      overlap_ratio=OVERLAP, max_iter=MAX_ITER, tol=TOL)

x_ras = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]

rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
max_error = np.max(np.abs(x_ras - x_exact))
rel_res = np.linalg.norm(b - A_sp @ x_ras) / np.linalg.norm(b)

label = f"n={N_SIDE} nsd={NSD} ratio={OVERLAP:.0%}"
status = "PASS" if converged and rel_error < 1e-4 else "FAIL"
INFO(f"[{status}] {label}: iters={iters} converged={converged} "
     f"rel_err={rel_error:.2e} max_err={max_error:.2e} rel_res={rel_res:.2e}")

get_agent().stop()
assert converged, f"{label}: did not converge"
assert rel_error < 1e-4, f"{label}: rel_error={rel_error:.2e}"
assert rel_res < 1e-4, f"{label}: rel_res={rel_res:.2e}"
'''

os.makedirs(LOG_DIR, exist_ok=True)

passed = 0
failed = 0
results = []

for n_side, nsd, overlap, max_iter, tol in CONFIGS:
    label = f"n={n_side} nsd={nsd} ratio={overlap:.0%}"
    script_path = os.path.join('/tmp', f'_golden_n{n_side}_sd{nsd}_r{int(overlap*100)}.py')

    # Generate runner script with constants
    code = f'N_SIDE={n_side}\nNSD={nsd}\nOVERLAP={overlap}\nMAX_ITER={max_iter}\nTOL={tol}\n'
    code += RUNNER_SCRIPT

    with open(script_path, 'w') as f:
        f.write(code)

    log_file = os.path.join(LOG_DIR, f'golden_n{n_side}_sd{nsd}_r{int(overlap*100)}.log')

    print(f"  Running {label}...", flush=True)
    result = subprocess.run(
        [FLY_BIN, '--log-dir', os.path.join(LOG_DIR, f'n{n_side}_sd{nsd}'), script_path],
        capture_output=True, text=True, timeout=300, cwd=PROJECT_ROOT,
    )

    with open(log_file, 'w') as f:
        f.write(result.stdout)
        f.write('\n--- STDERR ---\n')
        f.write(result.stderr)

    if result.returncode == 0:
        passed += 1
        results.append(f"  ✓ PASS  {label}")
        print(f"  ✓ PASS  {label}", flush=True)
    else:
        failed += 1
        results.append(f"  ✗ FAIL  {label}")
        print(f"  ✗ FAIL  {label}", flush=True)
        # Print last lines of log for debugging
        lines = result.stdout.strip().split('\n')
        for line in lines[-10:]:
            print(f"    {line}", flush=True)

print(f"\n{'='*60}")
print(f"Results: {passed} passed, {failed} failed")
for r in results:
    print(r)

assert failed == 0, f"{failed} golden data tests failed"
print(f"\n[ALL PASS] Golden data verification: {len(CONFIGS)} configs")
