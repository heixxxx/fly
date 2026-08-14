import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_db"


def cleanup():
    for p in [DB_PATH, DB_PATH + "_frozen", DB_PATH + "_blocked",
              DB_PATH + "_fanout", "/tmp/fly_e2e_concurrent"]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_completed(master, expected, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        c = master.completed_tasks
        if len(c) >= expected:
            return c
        time.sleep(0.5)
    return master.completed_tasks


def setup_master():
    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()
    master.launch_local_workers([{"role": "hybrid"}])
    assert master.wait_workers_registered(timeout=60)
    assert master._agent.get_connection_count() >= 1, "Worker not connected"
    return master


def setup_master_n_workers(n):
    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()
    configs = [{"role": "hybrid"} for _ in range(n)]
    master.launch_local_workers(configs)
    assert master.wait_workers_registered(timeout=60)
    assert master._agent.get_connection_count() >= n, \
        f"Only {master._agent.get_connection_count()}/{n} workers connected"
    return master
