#!/usr/bin/env python3
"""Large matrix sweep: RAS solver with multiple nsd/ratio configs.
Runs each config in a separate fly subprocess with timeout.
Reports: iters, time, rel_err (vs direct solve when feasible).
"""
import subprocess, os, sys, time, shutil, glob, re
from datetime import datetime

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLY_BIN = os.path.join(PROJECT_ROOT, 'build', 'bin', 'fly')
LOG_BASE = os.path.join(PROJECT_ROOT, 'qa', 'logs', 'sweep_large')

TIMEOUT_PER_CONFIG = {
    1000: 1200,  # 20 min (nsd=2 LU is slow for large subdomains)
    2000: 3600,  # 60 min
    3000: 3600,  # 60 min
}

# Configs to test
CONFIGS = []
for n in [1000, 2000, 3000]:
    for nsd in [2, 4, 8]:
        for ratio in [0.50, 0.60, 0.80]:
            if n >= 2000 and nsd == 2:
                continue
            CONFIGS.append((n, nsd, ratio))

# For n=3000, no golden data (OOM), skip rel_err
HAS_GOLDEN = {1000: True, 2000: True, 3000: False}

TASK_RUNNER = '''import sys, os, time, shutil
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu
from _fly_log import INFO

DB_PATH = f"/tmp/fly_sweep_large_n{N_SIDE}_sd{NSD}_r{int(OVERLAP*100)}"
from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph

def build_poisson_2d(n):
    N = n * n
    diags = [4.0*np.ones(N),-1.0*np.ones(N-1),-1.0*np.ones(N-1),-1.0*np.ones(N-n),-1.0*np.ones(N-n)]
    A = sparse.diags(diags,[0,1,-1,n,-n],shape=(N,N),format='lil')
    for i in range(1,n): A[i*n-1,i*n]=0.0; A[i*n,i*n-1]=0.0
    A_csc = A.tocsc()
    rows,cols,vals = [],[],[]
    for k in range(A_csc.shape[1]):
        s,e = A_csc.indptr[k],A_csc.indptr[k+1]
        for p in range(s,e):
            rows.append(int(A_csc.indices[p]))
            cols.append(int(k))
            vals.append(float(A_csc.data[p]))
    return N, rows, cols, vals, A_csc

# Build matrix
N_val, rows, cols, vals, A_csc = build_poisson_2d(N_SIDE)
b = [1.0] * N_val
INFO(f"Matrix built: n={N_SIDE} N={N_val} nnz={len(vals)}")

# Golden (if feasible)
x_exact = None
if COMPUTE_GOLDEN:
    INFO("Computing golden solution via direct solve...")
    t0 = time.time()
    A_sp = sparse.csc_matrix((vals,(rows,cols)), shape=(N_val,N_val))
    x_exact = splu(A_sp).solve(np.array(b))
    INFO(f"Golden solve: {time.time()-t0:.1f}s")
else:
    INFO(f"Skipping golden (n={N_SIDE} too large for direct solve)")

# Setup fly
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)
get_config().set_int('fail_unscheduleable_tasks', 1)
get_config().set_int('heartbeat_timeout', 3600000)  # 1 hour
master = get_agent()
master.launch_local_workers([{}]*NSD)
assert master.wait_for_workers(NSD), f"Failed to start {NSD} workers"

# Solve
INFO(f"Starting RAS: nsd={NSD} overlap_ratio={OVERLAP} max_iter={MAX_ITER}")
t0 = time.time()
db = open_db(DB_PATH)
sol = solve_ras_graph(db, N_val, rows, cols, vals, b, NSD,
                      overlap_ratio=OVERLAP, max_iter=MAX_ITER, tol=TOL)
t_solve = time.time() - t0

x_ras = np.array(sol['x'])
iters = sol['iters']
converged = sol['converged']
status = 'CONV' if converged else 'FAIL'

if x_exact is not None:
    rel_err = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
    rel_res = np.linalg.norm(np.array(b) - A_sp @ x_ras) / np.linalg.norm(b)
    INFO(f"RESULT|n={N_SIDE}|nsd={NSD}|ratio={OVERLAP:.2f}|iters={iters}|{status}|rel_err={rel_err:.2e}|rel_res={rel_res:.2e}|time={t_solve:.1f}")
else:
    # Compute relative residual only
    A_sp = sparse.csc_matrix((vals,(rows,cols)), shape=(N_val,N_val))
    rel_res = np.linalg.norm(np.array(b) - A_sp @ x_ras) / np.linalg.norm(b)
    INFO(f"RESULT|n={N_SIDE}|nsd={NSD}|ratio={OVERLAP:.2f}|iters={iters}|{status}|rel_res={rel_res:.2e}|time={t_solve:.1f}")

master.stop()
'''

def find_master_log(log_dir):
    """Find master.log, handling fly's log rotation (.latest symlink, .1 suffix)"""
    # Try .latest symlink first (fly creates this)
    latest = os.path.join(log_dir, log_dir.split('/')[-1] + '.latest')
    if os.path.islink(latest):
        real = os.readlink(latest)
        if not os.path.isabs(real):
            real = os.path.join(os.path.dirname(log_dir), real)
        mf = os.path.join(real, 'master.log')
        if os.path.isfile(mf):
            return mf
    # Try direct
    mf = os.path.join(log_dir, 'master.log')
    if os.path.isfile(mf):
        return mf
    # Try .1 suffix
    mf = os.path.join(log_dir + '.1', 'master.log')
    if os.path.isfile(mf):
        return mf
    return None


def parse_result(log_path):
    """Parse RESULT line from master.log"""
    mf = find_master_log(log_path)
    if mf is None:
        return None
    with open(mf) as f:
        for line in f:
            if 'RESULT|' in line:
                raw = line.split('RESULT|')[1].strip()
                parts = raw.split('|')
                d = {}
                status = '?'
                for i, p in enumerate(parts):
                    kv = p.split('=', 1)
                    if len(kv) == 2:
                        d[kv[0]] = kv[1]
                    else:
                        # Non-kv field is status
                        if p in ('CONV', 'FAIL'):
                            status = p
                return {
                    'n': int(d.get('n', 0)),
                    'nsd': int(d.get('nsd', 0)),
                    'ratio': float(d.get('ratio', 0)),
                    'iters': int(d.get('iters', -1)),
                    'status': status,
                    'time': float(d.get('time', -1)),
                    'rel_err': d.get('rel_err', 'N/A'),
                    'rel_res': d.get('rel_res', 'N/A'),
                }
    return None


def parse_stderr_result(stderr_path):
    if not os.path.isfile(stderr_path):
        return None
    with open(stderr_path) as f:
        for line in f:
            if 'RESULT|' in line:
                raw = line.split('RESULT|')[1].strip()
                parts = raw.split('|')
                d = {}
                status = '?'
                for p in parts:
                    kv = p.split('=', 1)
                    if len(kv) == 2:
                        d[kv[0]] = kv[1]
                    elif p in ('CONV', 'FAIL'):
                        status = p
                return {
                    'n': int(d.get('n', 0)),
                    'nsd': int(d.get('nsd', 0)),
                    'ratio': float(d.get('ratio', 0)),
                    'iters': int(d.get('iters', -1)),
                    'status': status,
                    'time': float(d.get('time', -1)),
                    'rel_err': d.get('rel_err', 'N/A'),
                    'rel_res': d.get('rel_res', 'N/A'),
                }
    return None


def run_config(n, nsd, ratio, max_iter=100, tol=1e-8):
    tag = f"n{n}_sd{nsd}_r{int(ratio*100)}"
    log_dir = os.path.join(LOG_BASE, tag)
    os.makedirs(log_dir, exist_ok=True)

    # Check if already completed
    existing = parse_result(log_dir)
    if existing is None:
        existing = parse_stderr_result(os.path.join(log_dir, 'stderr.log'))
    if existing and existing['status'] == 'CONV':
        print(f"  SKIP {tag} (already CONV: {existing['iters']} iters)")
        return existing

    compute_golden = HAS_GOLDEN.get(n, False)
    script_path = f"/tmp/_sw_large_{tag}.py"
    code = f"N_SIDE={n}\nNSD={nsd}\nOVERLAP={ratio}\nMAX_ITER={max_iter}\nTOL={tol}\nCOMPUTE_GOLDEN={compute_golden}\n" + TASK_RUNNER
    with open(script_path, 'w') as f:
        f.write(code)

    timeout = TIMEOUT_PER_CONFIG.get(n, 1800)
    stderr_path = os.path.join(log_dir, 'stderr.log')
    print(f"  RUN  {tag} (timeout={timeout}s)...", end='', flush=True)
    t0 = time.time()
    try:
        with open(stderr_path, 'w') as stderr_file:
            proc = subprocess.Popen(
                [FLY_BIN, '--log-dir', log_dir, script_path],
                stdout=subprocess.DEVNULL, stderr=stderr_file,
                cwd=PROJECT_ROOT
            )
            try:
                proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        elapsed = time.time() - t0
    except Exception as e:
        elapsed = time.time() - t0
        print(f" ERROR: {e}")
        return None

    result = parse_result(log_dir)
    if result is None:
        result = parse_stderr_result(stderr_path)
    if result:
        marker = '✓' if result['status'] == 'CONV' else '✗'
        err_str = f"rel_err={result['rel_err']}" if result['rel_err'] != 'N/A' else f"rel_res={result['rel_res']}"
        print(f" {marker} {result['status']} {result['iters']} iters {result['time']:.1f}s {err_str}")
    else:
        print(f" ? (no RESULT line, elapsed={elapsed:.0f}s)")
    return result


def main():
    os.makedirs(LOG_BASE, exist_ok=True)
    results = []

    # Filter configs: only run what's requested
    target_n = set()
    if len(sys.argv) > 1:
        target_n = set(int(x) for x in sys.argv[1:])
    else:
        target_n = {1000, 2000, 3000}

    configs = [(n, nsd, ratio) for n, nsd, ratio in CONFIGS if n in target_n]

    print("=== Large Matrix RAS Sweep ===")
    print(f"Configs: {len(configs)}")
    print("Direct solve baselines: n=1000→12.5s, n=2000→86.3s, n=3000→OOM")
    print(f"Start: {datetime.now().strftime('%H:%M:%S')}")
    print()

    for i, (n, nsd, ratio) in enumerate(configs):
        tag = f"n{n}_sd{nsd}_r{int(ratio*100)}"
        print(f"[{i+1}/{len(configs)}] {tag}")
        r = run_config(n, nsd, ratio)
        if r:
            results.append(r)
        print()

    # Summary
    print("\n" + "=" * 80)
    print("SWEEP RESULTS SUMMARY")
    print("=" * 80)
    print(f"{'n':>5} {'nsd':>4} {'ratio':>6} {'iters':>6} {'status':>5} {'time':>8} {'direct':>8} {'speedup':>8}")
    print("-" * 75)

    direct_times = {1000: 12.5, 2000: 86.3, 3000: float('inf')}

    for r in sorted(results, key=lambda x: (x['n'], x['nsd'], x['ratio'])):
        dt = direct_times.get(r['n'], float('inf'))
        su = f"{dt / r['time']:.1f}x" if r['time'] > 0 and dt < float('inf') else "N/A"
        marker = '✓' if r['status'] == 'CONV' else '✗'
        print(f"{marker} {r['n']:>5} {r['nsd']:>4} {r['ratio']:>5.0%} {r['iters']:>6} {r['status']:>5} {r['time']:>7.1f}s {dt:>7.1f}s {su:>8}")

    print()
    print("BEST per n (CONV, min time):")
    for n in sorted(target_n):
        conv = [r for r in results if r['n'] == n and r['status'] == 'CONV' and r['iters'] <= 100]
        conv.sort(key=lambda x: x['time'])
        if conv:
            b = conv[0]
            dt = direct_times.get(b['n'], float('inf'))
            su = f"{dt / b['time']:.1f}x" if dt < float('inf') else "N/A"
            print(f"  n={b['n']} nsd={b['nsd']} ratio={b['ratio']:.0%} → {b['iters']} iters, {b['time']:.1f}s (direct={dt:.1f}s, speedup={su})")
        else:
            print(f"  n={n}: no CONV config")


if __name__ == '__main__':
    main()
