"""Distributed RAS solver scaling test: n=1000,1200,1500 × nsd=4,5,6 (coarse only).

Each (n, nsd) configuration is run as an independent subprocess with its own DB
to avoid worker cache conflicts. Matrices are pre-generated in big_qa/matrices/.

Usage:
  python big_qa/test_scaling_coarse.py
  # Or a single case:
  python big_qa/test_scaling_coarse.py --only 1000 4
"""
import subprocess
import sys
import os
import time
import shutil

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FLY_BIN = os.path.join(PROJECT_ROOT, 'build', 'bin', 'fly')
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs')
MATRICES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'matrices')
os.makedirs(LOG_DIR, exist_ok=True)

SIZES = [1000, 1200, 1500]
NSDS = [4, 5, 6]
OVERLAP = 0.20
MAX_ITER = 100
TOL = 1e-8

RUNNER_SCRIPT = '''"""Single distributed RAS solver test case."""
import sys, os, shutil, time, resource
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))

import numpy as np
from scipy import sparse
from _fly_log import INFO

DB_PATH = f"/tmp/fly_bigqa_n{N_SIDE}_sd{NSD}_coarse"
MATRIX_PATH = MATRIX_PATH_VAL

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)

# Load matrix for verification
golden = np.load(MATRIX_PATH, allow_pickle=False)
x_exact = golden["x_exact"]
rows, cols, vals = golden["rows"], golden["cols"], golden["vals"]
b = golden["b"]
N_val = int(golden["N"])
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))

def mem_gb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (1024**3)

mem_before = mem_gb()
t0 = time.time()
db = open_db(DB_PATH)
sol = solve_ras_graph(db, MATRIX_PATH, NSD,
                      overlap_ratio=OVERLAP, max_iter=MAX_ITER, tol=TOL,
                      omega="coarse")
t_solve = time.time() - t0
mem_after = mem_gb()

x_ras = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]

rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
max_error = np.max(np.abs(x_ras - x_exact))
rel_res = np.linalg.norm(b - A_sp @ x_ras) / np.linalg.norm(b)

label = f"n={N_SIDE} nsd={NSD} coarse"
status = "PASS" if converged and rel_error < 1e-4 else "FAIL"
result_str = (f"[RESULT] {label}: iters={iters} converged={converged} "
              f"rel_err={rel_error:.2e} max_err={max_error:.2e} rel_res={rel_res:.2e} "
              f"solve={t_solve:.1f}s mem={mem_after:.1f}GB")
INFO(result_str)
print(result_str, flush=True)

get_agent().stop()
assert converged, f"{label}: did not converge in {iters} iters"
assert rel_error < 1e-4, f"{label}: rel_error={rel_error:.2e}"
'''


def run_case(n_side, nsd):
    label = f"n={n_side} nsd={nsd} coarse"
    matrix_path = os.path.join(MATRICES_DIR, f'poisson_n{n_side}.npz')
    if not os.path.exists(matrix_path):
        print(f"  SKIP {label}: matrix file not found at {matrix_path}")
        return None

    script_path = os.path.join('/tmp', f'_bigqa_n{n_side}_sd{nsd}_coarse.py')
    code = (f'N_SIDE={n_side}\nNSD={nsd}\nOVERLAP={OVERLAP}\n'
            f'MAX_ITER={MAX_ITER}\nTOL={TOL}\n'
            f'MATRIX_PATH_VAL="{matrix_path}"\n')
    code += RUNNER_SCRIPT
    with open(script_path, 'w') as f:
        f.write(code)

    log_dir = os.path.join(LOG_DIR, f'n{n_side}_sd{nsd}_coarse')

    print(f"  Running {label}...", end='', flush=True)
    t_start = time.time()
    try:
        result = subprocess.run(
            [FLY_BIN, '--log-dir', log_dir, script_path],
            capture_output=True, text=True, timeout=3600, cwd=PROJECT_ROOT,
        )
    except subprocess.TimeoutExpired:
        print(f" TIMEOUT (1h)", flush=True)
        return {'label': label, 'status': 'TIMEOUT'}

    elapsed = time.time() - t_start

    if result.returncode == 0:
        result_line = ""
        # Try stdout first
        for line in result.stdout.split('\n'):
            if '[RESULT]' in line:
                result_line = line.split('INFO] ')[-1] if 'INFO]' in line else line
                break
        # Fallback: read from log file
        if not result_line:
            master_log = os.path.join(log_dir, 'master.log')
            if os.path.exists(master_log):
                with open(master_log) as f:
                    for line in f:
                        if '[RESULT]' in line:
                            result_line = line.split('INFO] ')[-1].strip() if 'INFO]' in line else line.strip()
                            break
        print(f" OK  {result_line}", flush=True)
        return {'label': label, 'status': 'PASS', 'output': result_line, 'elapsed': elapsed}
    else:
        print(f" FAIL", flush=True)
        lines = result.stdout.strip().split('\n')
        for line in lines[-15:]:
            if any(k in line for k in ['ERROR', 'Error', 'FAIL', 'Traceback', 'Assertion']):
                print(f"    {line}", flush=True)
        stderr_lines = result.stderr.strip().split('\n')[-5:]
        for line in stderr_lines:
            if line.strip():
                print(f"    [stderr] {line}", flush=True)
        return {'label': label, 'status': 'FAIL', 'elapsed': elapsed}


def main():
    # Parse --only n nsd
    only = None
    if '--only' in sys.argv:
        idx = sys.argv.index('--only')
        only = (int(sys.argv[idx + 1]), int(sys.argv[idx + 2]))

    print("=" * 70)
    print("Distributed RAS Solver Scaling Test (coarse correction only)")
    print(f"Sizes: {SIZES}  NSDs: {NSDS}  overlap={OVERLAP}  tol={TOL}  max_iter={MAX_ITER}")
    print("=" * 70)

    cases = []
    for n in SIZES:
        for nsd in NSDS:
            if only and (n, nsd) != only:
                continue
            cases.append((n, nsd))

    results = []
    for n, nsd in cases:
        r = run_case(n, nsd)
        if r:
            results.append(r)

    # Summary
    print(f"\n{'='*70}")
    print("Summary")
    print(f"{'='*70}")
    passed = sum(1 for r in results if r['status'] == 'PASS')
    failed = sum(1 for r in results if r['status'] == 'FAIL')
    timeout = sum(1 for r in results if r['status'] == 'TIMEOUT')
    for r in results:
        tag = r['status']
        extra = f" {r.get('output', '')}" if tag == 'PASS' else ""
        print(f"  [{tag}] {r['label']}{extra}")

    print(f"\nResults: {passed} passed, {failed} failed, {timeout} timeout")
    assert failed == 0 and timeout == 0, f"{failed} failed, {timeout} timed out"
    print(f"\n[ALL PASS] {passed}/{len(results)} cases passed")


if __name__ == '__main__':
    main()
