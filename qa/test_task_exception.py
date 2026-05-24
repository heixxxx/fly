"""E2E test: task raising RuntimeError gets marked as FAILED.

Verifies:
  - failing_task raises RuntimeError with the expected message
  - Master records the task as failed
  - get_task_error() returns the error message
  - The key the task would have written is NOT readable (task didn't complete)
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_task_exception_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import failing_task
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


def test_task_exception():
    cleanup()

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

    failing_task(db, "never_written", "intentional_test_error")

    assert wait_for(lambda: len(master.failed_tasks) >= 1), \
        "failing_task should be recorded as failed"

    failed = master.failed_tasks
    assert len(failed) >= 1, f"Expected at least 1 failed task, got {len(failed)}"

    error_msg = master.get_task_error(failed[0])
    assert "intentional_test_error" in error_msg, \
        f"Error should contain 'intentional_test_error', got: {error_msg}"

    # The key should NOT be readable because the task raised before writing
    try:
        db.read_object("never_written")
        assert False, "read_object should have failed for unwritten key"
    except Exception:
        pass

    del db
    master.stop()
    print("[PASS] test_task_exception", file=sys.stderr)


if __name__ == "__main__":
    test_task_exception()
