"""RAS solver benchmark: n=500,700 with varying workers."""
from _fly_log import INFO
import sys
import os
import shutil
import time
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras


def run_one(n, nsd, overlap=None):
    db_path = f"/tmp/fly_bench_ras_n{n}_nsd{nsd}"
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    db = open_db(db_path)
    t0 = time.time()
    result = solve_ras(db, n, nsd, overlap)
    elapsed = time.time() - t0

    x_ras = np.array(result["x"])
    INFO(f"  n={n} nsd={nsd} ov={result.get('overlap','?')}: "
         f"iters={result['iters']} converged={result['converged']} "
         f"res={result['residual']:.2e} time={elapsed:.3f}s")

    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    return elapsed, result


# ── Main ──

get_config().set_int("fail_unscheduleable_tasks", 1)

# Read config from env
N = int(os.environ.get("BENCH_N", "500"))
NSD = int(os.environ.get("BENCH_NSD", "4"))
TRIALS = int(os.environ.get("BENCH_TRIALS", "3"))

INFO(f"=== RAS Benchmark: n={N} nsd={NSD} trials={TRIALS} ===")
INFO(f"Matrix size: {N*N}x{N*N}")

master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), f"Failed to connect {NSD} workers"
INFO(f"Workers connected: {master.worker_count}")

times = []
for trial in range(TRIALS):
    INFO(f"--- Trial {trial+1}/{TRIALS} ---")
    elapsed, result = run_one(N, NSD)
    times.append(elapsed)
    INFO(f"  Trial {trial+1}: {elapsed:.3f}s")

avg = sum(times) / len(times)
INFO(f"=== RESULT: n={N} nsd={NSD} avg={avg:.3f}s min={min(times):.3f}s max={max(times):.3f}s ===")

master.stop()
INFO(f"[DONE] RAS benchmark complete")
