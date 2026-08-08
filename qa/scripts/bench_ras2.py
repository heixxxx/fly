"""Benchmark: 2-Level RAS solver."""
from _fly_log import INFO
import os
import shutil
import time


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras2


def run_one(n, nsd):
    db_path = f"/tmp/fly_bench_ras2_n{n}_nsd{nsd}"
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    db = open_db(db_path)
    t0 = time.time()
    result = solve_ras2(db, n, nsd)
    elapsed = time.time() - t0

    INFO(f"  ras2 n={n} nsd={nsd}: "
         f"iters={result['iters']:3d} conv={result['converged']} "
         f"res={result['residual']:.2e} time={elapsed:.1f}s")

    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)
    return elapsed, result


get_config().set_int("fail_unscheduleable_tasks", 1)

N = int(os.environ.get("BENCH_N", "100"))
NSD = int(os.environ.get("BENCH_NSD", "4"))

INFO(f"=== RAS2 Benchmark: n={N} nsd={NSD} ===")
INFO(f"Matrix size: {N*N}x{N*N}")

master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), f"Failed to connect {NSD} workers"
INFO(f"Workers connected: {master.worker_count}")

run_one(N, NSD)

master.stop()
INFO("[DONE] RAS2 benchmark complete")
