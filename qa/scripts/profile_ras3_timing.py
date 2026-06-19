"""Profile ras3 task scheduling overhead: measure wall time between tasks."""
from _fly_log import INFO, ERR
import os
import shutil
import time
import re


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras, solve_ras3

get_config().set_int("fail_unscheduleable_tasks", 1)

N = int(os.environ.get("P_N", "20"))
NSD = int(os.environ.get("P_NSD", "2"))

INFO(f"=== Timing Profile: n={N} nsd={NSD} ===")

master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), f"Failed to connect {NSD} workers"
INFO(f"Workers connected: {master.worker_count}")

# Run RAS (baseline, 2-phase)
db_path = f"/tmp/fly_profile_ras_n{N}_nsd{NSD}"
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)
db = open_db(db_path)
t0 = time.perf_counter()
result_ras = solve_ras(db, n=N, num_subdomains=NSD)
t_ras = time.perf_counter() - t0
INFO(f"RAS:  iters={result_ras['iters']} conv={result_ras['converged']} "
     f"res={result_ras['residual']:.2e} time={t_ras:.3f}s")
del db
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

# Run RAS3 (4-phase)
db_path = f"/tmp/fly_profile_ras3_n{N}_nsd{NSD}"
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)
db = open_db(db_path)
t0 = time.perf_counter()
result_ras3 = solve_ras3(db, n=N, num_subdomains=NSD)
t_ras3 = time.perf_counter() - t0
INFO(f"RAS3: iters={result_ras3['iters']} conv={result_ras3['converged']} "
     f"res={result_ras3['residual']:.2e} time={t_ras3:.3f}s")
del db
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

INFO(f"\n=== Timing Summary ===")
INFO(f"RAS:  {t_ras:.3f}s")
INFO(f"RAS3: {t_ras3:.3f}s")
INFO(f"Ratio: {t_ras3/t_ras:.2f}x")

master.stop()
INFO("[DONE]")
