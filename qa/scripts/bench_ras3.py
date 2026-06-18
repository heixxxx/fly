"""RAS3 (distributed matvec) benchmark: compare with ras.py (centralized matvec)."""
from _fly_log import INFO, ERR
import sys
import os
import shutil
import time
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras, solve_ras3


def run_ras(n, nsd, overlap=None):
    db_path = f"/tmp/fly_bench_ras3_ras_n{n}_nsd{nsd}"
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)
    db = open_db(db_path)
    t0 = time.time()
    result = solve_ras(db, n, nsd, overlap)
    elapsed = time.time() - t0
    x = np.array(result["x"])
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)
    return elapsed, result


def run_ras3(n, nsd, overlap=None):
    db_path = f"/tmp/fly_bench_ras3_n{n}_nsd{nsd}"
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)
    db = open_db(db_path)
    t0 = time.time()
    result = solve_ras3(db, n, nsd, overlap)
    elapsed = time.time() - t0
    x = np.array(result["x"])
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)
    return elapsed, result


get_config().set_int("fail_unscheduleable_tasks", 1)

N = int(os.environ.get("BENCH_N", "500"))
NSD = int(os.environ.get("BENCH_NSD", "4"))
TRIALS = int(os.environ.get("BENCH_TRIALS", "1"))

INFO(f"=== RAS3 vs RAS Benchmark: n={N} nsd={NSD} trials={TRIALS} ===")
INFO(f"Matrix size: {N*N}x{N*N}")

master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), f"Failed to connect {NSD} workers"
INFO(f"Workers connected: {master.worker_count}")

configs = [(N, NSD)]

# First run ras.py to get baseline
INFO(f"\n--- RAS (centralized matvec) ---")
for n, nsd in configs:
    times_ras = []
    for trial in range(TRIALS):
        INFO(f"  ras trial {trial+1}/{TRIALS}: n={n} nsd={nsd}")
        try:
            elapsed, result = run_ras(n, nsd)
            times_ras.append(elapsed)
            INFO(f"    result: iters={result['iters']} conv={result['converged']} "
                 f"res={result['residual']:.2e} time={elapsed:.3f}s")
        except Exception as e:
            ERR(f"    FAILED: {e}")
            times_ras.append(None)

# Then run ras3.py
INFO(f"\n--- RAS3 (distributed matvec) ---")
for n, nsd in configs:
    times_ras3 = []
    for trial in range(TRIALS):
        INFO(f"  ras3 trial {trial+1}/{TRIALS}: n={n} nsd={nsd}")
        try:
            elapsed, result = run_ras3(n, nsd)
            times_ras3.append(elapsed)
            INFO(f"    result: iters={result['iters']} conv={result['converged']} "
                 f"res={result['residual']:.2e} time={elapsed:.3f}s")
        except Exception as e:
            ERR(f"    FAILED: {e}")
            times_ras3.append(None)

# Compare
valid_ras = [t for t in times_ras if t is not None]
valid_ras3 = [t for t in times_ras3 if t is not None]
if valid_ras and valid_ras3:
    avg_ras = sum(valid_ras) / len(valid_ras)
    avg_ras3 = sum(valid_ras3) / len(valid_ras3)
    speedup = avg_ras / avg_ras3 if avg_ras3 > 0 else 0
    INFO(f"\n=== RESULT: n={N} nsd={NSD} ===")
    INFO(f"  RAS  avg={avg_ras:.3f}s  ({len(valid_ras)}/{TRIALS} trials)")
    INFO(f"  RAS3 avg={avg_ras3:.3f}s  ({len(valid_ras3)}/{TRIALS} trials)")
    INFO(f"  Speedup: {speedup:.2f}x")
else:
    ERR("Not enough valid trials to compare")

master.stop()
INFO("[DONE] RAS3 vs RAS benchmark complete")
