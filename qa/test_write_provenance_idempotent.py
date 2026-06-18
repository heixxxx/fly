"""E2E test: write provenance - same task rerun is idempotent.

Submit write_data(db, key, 42) twice with identical args.
Both should succeed — same task + same args = same hash → provenance match.
"""
from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_provenance_idempotent_db_{os.getpid()}"

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


def test_write_provenance_idempotent():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])

    assert master.wait_for_workers(1), \
        "Worker should connect"

    db = open_db(DB_PATH)

    write_data(db, "prov_key", 42)

    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        f"First write should complete, got {len(master.completed_tasks)}"

    val1 = db.read_object("prov_key")
    assert val1 == 42, f"Expected 42, got {val1}"
    p1_completed = len(master.completed_tasks)

    write_data(db, "prov_key", 42)

    assert wait_for(lambda: len(master.completed_tasks) >= p1_completed + 1, timeout=30.0), \
        f"Second write (same args) should succeed, got {len(master.completed_tasks)} completed, {len(master.failed_tasks)} failed"

    val2 = db.read_object("prov_key")
    assert val2 == 42, f"Expected 42 after rerun, got {val2}"

    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    INFO("[PASS] test_write_provenance_idempotent: same task rerun accepted")


test_write_provenance_idempotent()
INFO("\nAll tests passed!")
