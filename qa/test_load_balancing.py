"""E2E test: multiple workers share task load in parallel.

Verifies:
  - 3 workers can connect to a single master
  - 30 independent tasks all complete successfully
  - Completion happens within reasonable time (multi-worker throughput)
"""
from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_load_balancing_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_load_balancing():
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}, {}])
    assert master.wait_for_workers(3), \
        "3 workers should connect"

    db = open_db(DB_PATH)

    # Submit 30 independent write tasks
    for i in range(30):
        write_data(db, f"key_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= 30, timeout=40.0), \
        f"All 30 tasks should complete, got {len(master.completed_tasks)}"

    assert len(master.failed_tasks) == 0, \
        f"No tasks should fail, got {len(master.failed_tasks)} failed"

    # Spot-check a few values
    for i in [0, 14, 29]:
        val = db.read_object(f"key_{i}")
        assert val == i, f"Expected key_{i}={i}, got {val}"

    INFO("[PASS] test_load_balancing")


test_load_balancing()
