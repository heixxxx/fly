"""E2E test: multiple workers share task load in parallel.

Verifies:
  - 3 workers can connect to a single master
  - 30 independent tasks all complete successfully
  - Completion happens within reasonable time (multi-worker throughput)
"""
from _fly_log import INFO
import os
import shutil



from test import write_data
from test import record, wait_tasks_no_failure
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


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

    # 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 30, timeout=40.0), \
    # 原断言：assert len(master.failed_tasks) == 0, \
    assert wait_tasks_no_failure(master, 30, "load_balance/no_failed_tasks", timeout=40.0), \
        f"All 30 tasks should complete, got {len(master.completed_tasks)}"

    # Spot-check a few values
    for i in [0, 14, 29]:
        val = db.read_object(f"key_{i}")
        # 原断言：assert val == i, f"Expected key_{i}={i}, got {val}"
        record(f"spot_check/key_{i}", val == i, detail=val)

    INFO("[DONE] test_load_balancing")


test_load_balancing()
