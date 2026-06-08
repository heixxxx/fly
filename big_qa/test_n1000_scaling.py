"""Test n=1000: omega=1.0 vs adaptive vs coarse. Each config as independent subprocess."""
import subprocess
import sys
import os

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FLY_BIN = os.path.join(PROJECT_ROOT, 'build', 'bin', 'fly')
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs')
os.makedirs(LOG_DIR, exist_ok=True)

RUNNER_SCRIPT = '''"""Single n=1000 test case."""
import sys, os, shutil, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

DB_PATH = f"/tmp/fly_bigqa_n{N_SIDE}_sd{NSD}_omega{str(OMEGA).replace('.', '_')}"
MATRIX_PATH = f"/tmp/fly_bigqa_matrix_n{N_SIDE}.npz"

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

t0 = time.time()
db = open_db(DB_PATH)
sol = solve_ras_graph(db, MATRIX_PATH, NSD,
                      overlap_ratio=OVERLAP, max_iter=MAX_ITER, tol=TOL,
                      omega=OMEGA)
t_solve = time.time() - t0

x_ras = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]

rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
max_error = np.max(np.abs(x_ras - x_exact))
rel_res = np.linalg.norm(b - A_sp @ x_ras) / np.linalg.norm(b)

label = f"n={N_SIDE} nsd={NSD} omega={OMEGA}"
status = "PASS" if converged and rel_error < 1e-4 else "FAIL"
INFO(f"[RESULT] {label}: iters={iters} converged={converged} "
     f"rel_err={rel_error:.2e} max_err={max_error:.2e} rel_res={rel_res:.2e} "
     f"solve={t_solve:.1f}s")

get_agent().stop()
assert converged, f"{label}: did not converge in {iters} iters"
assert rel_error < 1e-4, f"{label}: rel_error={rel_error:.2e}"
'''

CONFIGS = [
    (1000, 4, 0.50, 300, 1e-8),
    (1000, 8, 0.50, 300, 1e-8),
]

OMEGAS = [1.0, "adaptive"]

passed = 0
failed = 0
results = []

for omega_val in OMEGAS:
    for n_side, nsd, overlap, max_iter, tol in CONFIGS:
        label = f"n={n_side} nsd={nsd} omega={omega_val}"
        omega_tag = str(omega_val).replace('.', '_')
        script_path = os.path.join('/tmp', f'_bigqa_n{n_side}_sd{nsd}_o{omega_tag}.py')

        code = f'N_SIDE={n_side}\nNSD={nsd}\nOVERLAP={overlap}\nMAX_ITER={max_iter}\nTOL={tol}\nOMEGA={repr(omega_val)}\n'
        code += RUNNER_SCRIPT

        with open(script_path, 'w') as f:
            f.write(code)

        log_dir = os.path.join(LOG_DIR, f'n{n_side}_sd{nsd}_o{omega_tag}')

        print(f"  Running {label}...", end='', flush=True)
        result = subprocess.run(
            [FLY_BIN, '--log-dir', log_dir, script_path],
            capture_output=True, text=True, timeout=1800, cwd=PROJECT_ROOT,
        )

        if result.returncode == 0:
            passed += 1
            result_line = ""
            for line in result.stdout.split('\n'):
                if '[RESULT]' in line:
                    result_line = line.split('INFO] ')[-1] if 'INFO]' in line else line
                    break
            results.append(f"  ✓ PASS  {label}")
            print(f" ✓  {result_line}", flush=True)
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
print(f"\n[ALL PASS] n=1000 adaptive vs baseline: {len(CONFIGS) * len(OMEGAS)} configs")
