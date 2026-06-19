"""Parameter sweep: overlap_ratio × nsd → iterations + solve_time.
Goal: find optimal config where iters ≤ 100 and total time minimized."""
import subprocess
import os
import re
import json

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FLY_BIN = os.path.join(PROJECT_ROOT, 'build', 'bin', 'fly')
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs', 'sweep')
os.makedirs(LOG_DIR, exist_ok=True)

RUNNER_SCRIPT = '''"""Single sweep case."""
import sys, os, shutil, time

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

DB_PATH = f"/tmp/fly_sweep_n{N_SIDE}_sd{NSD}_r{int(OVERLAP*100)}"

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

N_val, rows, cols, vals, A_csc = build_poisson_2d(N_SIDE)
b = [1.0] * N_val
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))
x_exact = splu(A_sp).solve(np.array(b))

t0 = time.time()
db = open_db(DB_PATH)
sol = solve_ras_graph(db, N_val, rows, cols, vals, b, NSD,
                      overlap_ratio=OVERLAP, max_iter=MAX_ITER, tol=TOL)
t_solve = time.time() - t0

x_ras = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]
rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
rel_res = np.linalg.norm(np.array(b) - A_sp @ x_ras) / np.linalg.norm(b)

status = "CONV" if converged else "FAIL"
INFO(f"RESULT|n={N_SIDE}|nsd={NSD}|ratio={OVERLAP:.2f}|iters={iters}|{status}|"
     f"rel_err={rel_error:.2e}|rel_res={rel_res:.2e}|time={t_solve:.1f}")

master.stop()
'''

MAX_ITER = 100
TOL = 1e-8

sweep_configs = []
for n_side in [100, 500, 1000]:
    for nsd in [2, 4, 8]:
        if nsd > n_side:
            continue
        for ratio in [0.30, 0.40, 0.50, 0.60, 0.80]:
            sweep_configs.append((n_side, nsd, ratio))

print(f"Running {len(sweep_configs)} sweep configs (max_iter={MAX_ITER})")
print(f"{'n':>5} {'nsd':>4} {'ratio':>6} {'iters':>6} {'status':>6} {'time':>7} {'rel_err':>10}")
print("-" * 60)

results = []
for n_side, nsd, ratio in sweep_configs:
    script_path = f'/tmp/_sweep_n{n_side}_sd{nsd}_r{int(ratio*100)}.py'
    code = f'N_SIDE={n_side}\nNSD={nsd}\nOVERLAP={ratio}\nMAX_ITER={MAX_ITER}\nTOL={TOL}\n'
    code += RUNNER_SCRIPT
    with open(script_path, 'w') as f:
        f.write(code)

    log_dir = os.path.join(LOG_DIR, f'n{n_side}_sd{nsd}_r{int(ratio*100)}')
    timeout = 600 if n_side <= 500 else 1200

    result = subprocess.run(
        [FLY_BIN, '--log-dir', log_dir, script_path],
        capture_output=True, text=True, timeout=timeout, cwd=PROJECT_ROOT,
    )

    parsed = {"n": n_side, "nsd": nsd, "ratio": ratio, "iters": -1,
              "status": "ERROR", "time": -1, "rel_err": -1}

    master_log = os.path.join(log_dir, 'master.log')
    parse_src = result.stdout
    if os.path.isfile(master_log):
        with open(master_log) as f:
            parse_src = f.read()

    for line in parse_src.split('\n'):
        if 'RESULT|' in line:
            parts = line.split('RESULT|')[1].split('|')
            for p in parts:
                kv = p.split('=', 1)
                if len(kv) == 2:
                    k, v = kv
                    if k == 'iters': parsed['iters'] = int(v)
                    elif k == 'status': parsed['status'] = v
                    elif k == 'time': parsed['time'] = float(v)
                    elif k == 'rel_err': parsed['rel_err'] = float(v)
                    elif k == 'ratio': parsed['ratio'] = float(v)
            break

    results.append(parsed)
    print(f"{parsed['n']:>5} {parsed['nsd']:>4} {parsed['ratio']:>5.0%} "
          f"{parsed['iters']:>6} {parsed['status']:>6} {parsed['time']:>6.1f}s "
          f"{parsed['rel_err']:>10.2e}", flush=True)

print(f"\n{'='*60}")
print("OPTIMAL CONFIGS (converged ≤ 100 iters, min time):")
for n_side in [100, 500, 1000]:
    best = None
    for r in results:
        if r['n'] == n_side and r['status'] == 'CONV' and r['iters'] <= 100:
            if best is None or r['time'] < best['time']:
                best = r
    if best:
        print(f"  n={best['n']} nsd={best['nsd']} ratio={best['ratio']:.0%} → "
              f"{best['iters']} iters, {best['time']:.1f}s, rel_err={best['rel_err']:.2e}")
    else:
        print(f"  n={n_side}: NO config converged ≤ 100 iters")

with open(os.path.join(LOG_DIR, 'sweep_results.json'), 'w') as f:
    json.dump(results, f, indent=2)
