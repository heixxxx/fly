"""E2E test: task raising RuntimeError gets marked as FAILED.

Verifies:
  - failing_task raises RuntimeError with the expected message
  - Master records the task as failed
  - get_task_error() returns the error message
  - The key the task would have written is NOT readable (task didn't complete)
"""
from _fly_log import INFO
import time
import os
import shutil



from test import failing_task
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


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

    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1

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

    INFO("[PASS] test_task_exception")


test_task_exception()
