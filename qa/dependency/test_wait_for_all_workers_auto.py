"""Test: wait_for_all_workers() auto-detects worker count from launch_local_workers.

Verifies that wait_for_all_workers() no longer requires explicit count parameter —
it should automatically know how many workers were launched.
"""
from _fly_log import INFO
import time
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_wait_for_all_workers_auto_db_{os.getpid()}"


from e2e_tasks import write_data
from fly import open_db, get_config
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()

# Launch 2 workers
master.launch_local_workers([{}, {}])

# wait_for_all_workers should auto-detect count=2 — no explicit count needed
master.wait_for_all_workers()

# Verify workers are actually connected
assert master.worker_count >= 2, \
    f"Expected 2+ workers, got {master.worker_count}"

# Verify we can submit and run tasks
db = open_db(DB_PATH)
write_data(db, "k1", "v1")
assert wait_for(lambda: len(master.completed_tasks) >= 1)

master.stop()
INFO("[PASS] test_wait_for_all_workers_auto")
