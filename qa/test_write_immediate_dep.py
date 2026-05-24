"""E2E test: write_object on master immediately satisfies dependencies.

Verifies:
  - Master-side write_object triggers dependency satisfaction
  - Worker can read Master-written data via remote request (Tier 3)
  - Worker-written data also satisfies downstream dependencies
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_write_immediate_dep_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, read_data, compute_sum
from fly import open_db, get_config


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


def test_master_write_dep():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}])
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    # Phase 1: Master writes data directly (not via task)
    db.write_object("master_a", 10)
    db.write_object("master_b", 20)

    # Phase 2: Worker task reads Master-written data and writes result
    compute_sum(db, "master_a", "master_b", "sum_result")

    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        f"compute_sum should complete, got {len(master.completed_tasks)} completed"

    result = db.read_object("sum_result")
    assert result == 30, f"Expected 30, got {result}"

    del db
    master.stop()
    print("[PASS] test_master_write_dep: Worker reads Master-written data "
          "via Tier 3 remote request", file=sys.stderr)


def test_worker_write_dep():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}])
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    # Phase 1: Worker writes data via tasks
    write_data(db, "input_a", 10)
    write_data(db, "input_b", 20)

    assert wait_for(lambda: len(master.completed_tasks) >= 2), \
        "Phase 1: both writes should complete"

    # Phase 2: compute_sum reads worker-written data
    compute_sum(db, "input_a", "input_b", "sum_result")

    assert wait_for(lambda: len(master.completed_tasks) >= 3), \
        f"Phase 2: compute_sum should complete, got {len(master.completed_tasks)} completed"

    result = db.read_object("sum_result")
    assert result == 30, f"Expected 30, got {result}"

    del db
    master.stop()
    print("[PASS] test_worker_write_dep: data written by tasks satisfies "
          "downstream compute_sum", file=sys.stderr)


if __name__ == "__main__":
    test_master_write_dep()
    test_worker_write_dep()
    print("\nAll tests passed!")
