"""E2E test: write provenance - different task writing same key is rejected.

write_data(db, "clash_key", 42) succeeds.
write_data(db, "clash_key", 99) — same task name but different args
  → different hash → provenance mismatch → write rejected.
  The task still "completes" but the original value is preserved.
"""
from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_provenance_mismatch_db_{os.getpid()}"

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


def test_write_provenance_mismatch():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])

    assert master.wait_for_workers(1), \
        "Worker should connect"

    db = open_db(DB_PATH)

    write_data(db, "clash_key", 42)

    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        f"First write should complete, got {len(master.completed_tasks)}"

    val1 = db.read_object("clash_key")
    assert val1 == 42, f"Expected 42, got {val1}"

    write_data(db, "clash_key", 99)

    assert wait_for(lambda: len(master.completed_tasks) + len(master.failed_tasks) >= 2, timeout=30.0), \
        f"Second task should finish, got {len(master.completed_tasks)} completed, {len(master.failed_tasks)} failed"

    assert len(master.failed_tasks) >= 1, \
        f"Second write should fail (provenance mismatch), got {len(master.failed_tasks)} failed"

    val2 = db.read_object("clash_key")
    assert val2 == 42, \
        f"Original value should be preserved after mismatch, expected 42 got {val2}"

    INFO("[PASS] test_write_provenance_mismatch: different-args write rejected, original preserved")


test_write_provenance_mismatch()
INFO("\nAll tests passed!")
