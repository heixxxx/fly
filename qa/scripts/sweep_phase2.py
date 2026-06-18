"""Sweep Phase 2: fill missing configs and run n=1000."""
import subprocess, os, glob, re

PROJECT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
FLY_BIN = os.path.join(PROJECT_ROOT, 'build', 'bin', 'fly')
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'logs', 'sweep')
os.makedirs(LOG_DIR, exist_ok=True)

RUNNER_SCRIPT = '''"""Single sweep case."""
import sys, os, shutil, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src'))
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
    diags = [4.0 * np.ones(N), -1.0 * np.ones(N-1), -1.0 * np.ones(N-1),
             -1.0 * np.ones(N-n), -1.0 * np.ones(N-n)]
    A = sparse.diags(diags, [0, 1, -1, n, -n], shape=(N, N), format='lil')
    for i in range(1, n):
        A[i*n-1, i*n] = 0.0; A[i*n, i*n-1] = 0.0
    A_csc = A.tocsc()
    rows, cols, vals = [], [], []
    for k in range(A_csc.shape[1]):
        s, e = A_csc.indptr[k], A_csc.indptr[k+1]
        for p in range(s, e):
            rows.append(int(A_csc.indices[p])); cols.append(int(k))
            vals.append(float(A_csc.data[p]))
    return N, rows, cols, vals, A_csc

if os.path.isdir(DB_PATH): shutil.rmtree(DB_PATH, ignore_errors=True)
get_config().set_int("fail_unscheduleable_tasks", 1)
master = get_agent()
master.launch_local_workers([{}] * NSD)
assert master.wait_for_workers(NSD)

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
rel_err = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
rel_res = np.linalg.norm(np.array(b) - A_sp @ x_ras) / np.linalg.norm(b)
status = "CONV" if converged else "FAIL"
INFO(f"RESULT|n={N_SIDE}|nsd={NSD}|ratio={OVERLAP:.2f}|iters={iters}|{status}|"
     f"rel_err={rel_err:.2e}|rel_res={rel_res:.2e}|time={t_solve:.1f}")
master.stop()
'''

existing = set()
for d in glob.glob(os.path.join(LOG_DIR, 'n*')):
    mf = os.path.join(d, 'master.log')
    if os.path.isfile(mf):
        with open(mf) as f:
            for line in f:
                if 'RESULT|' in line and 'CONV' in line:
                    existing.add(os.path.basename(d))

all_configs = []
for n_side in [100, 500, 1000]:
    for nsd in [2, 4, 8]:
        for ratio in [0.30, 0.40, 0.50, 0.60, 0.80]:
            dirname = f'n{n_side}_sd{nsd}_r{int(ratio*100)}'
            if dirname not in existing:
                all_configs.append((n_side, nsd, ratio))

print(f"Need to run {len(all_configs)} missing configs")
print(f"{'n':>5} {'nsd':>4} {'ratio':>6} {'iters':>6} {'status':>6} {'time':>7}")
print("-" * 50)

for n_side, nsd, ratio in all_configs:
    dirname = f'n{n_side}_sd{nsd}_r{int(ratio*100)}'
    script_path = f'/tmp/_sweep_{dirname}.py'
    code = f'N_SIDE={n_side}\nNSD={nsd}\nOVERLAP={ratio}\nMAX_ITER=100\nTOL=1e-8\n'
    code += RUNNER_SCRIPT
    with open(script_path, 'w') as f:
        f.write(code)

    log_dir = os.path.join(LOG_DIR, dirname)
    timeout = 600 if n_side <= 500 else 1200
    print(f"{n_side:>5} {nsd:>4} {ratio:>5.0%} ", end='', flush=True)
    result = subprocess.run(
        [FLY_BIN, '--log-dir', log_dir, script_path],
        capture_output=True, text=True, timeout=timeout, cwd=PROJECT_ROOT)

    master_log = os.path.join(log_dir, 'master.log')
    parsed = {"iters": "?", "status": "ERROR", "time": "?"}
    if os.path.isfile(master_log):
        with open(master_log) as f:
            for line in f:
                if 'RESULT|' in line:
                    parts = line.split('RESULT|')[1].strip().split('|')
                    for p in parts:
                        kv = p.split('=', 1)
                        if len(kv) == 2:
                            parsed[kv[0]] = kv[1]
                    break

    print(f"{parsed.get('iters','?'):>6} {parsed.get('status','?'):>6} {parsed.get('time','?'):>7}s")

print(f"\n{'='*50}")
print("ALL RESULTS (including previously completed):")
for d in sorted(glob.glob(os.path.join(LOG_DIR, 'n*'))):
    mf = os.path.join(d, 'master.log')
    if not os.path.isfile(mf): continue
    with open(mf) as f:
        for line in f:
            if 'RESULT|' in line:
                r = line.split('[INFO] ')[-1].rstrip() if '[INFO] ' in line else line.rstrip()
                print(f"  {r}")
                break
