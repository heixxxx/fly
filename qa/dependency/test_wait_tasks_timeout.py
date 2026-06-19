"""E2E test: wait_tasks timeout behavior.

Tests:
  1. wait_tasks(timeout=2.0) returns gracefully (no raise) when no tasks pending
  2. wait_tasks(timeout=1.0) with a submitted task returns partial results (no raise)
"""
from _fly_log import INFO
import time
import os
import shutil



from e2e_tasks import write_data
from fly import open_db, wait_tasks, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_wait_tasks_no_pending_returns_gracefully():
    """wait_tasks(timeout=2.0) with no tasks should return without raising."""
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1, \
        "Worker should connect to master"

    result = wait_tasks(timeout=2.0)
    assert result is not None, "wait_tasks should return a list, not None"

    INFO("[PASS] test_wait_tasks_no_pending_returns_gracefully: "
          "wait_tasks returns without error when no tasks pending")


def test_wait_tasks_short_timeout_with_task():
    """wait_tasks(timeout=1.0) with a submitted task returns without raising."""
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1, \
        "Worker should connect to master"

    db = open_db(DB_PATH)
    write_data(db, "timeout_key", 42)

    result = wait_tasks(timeout=1.0)
    assert result is not None, "wait_tasks should return a list, not None"

    INFO("[PASS] test_wait_tasks_short_timeout_with_task: "
          "wait_tasks returns without error even with short timeout")


test_wait_tasks_no_pending_returns_gracefully()
INFO("")
test_wait_tasks_short_timeout_with_task()
INFO("\nAll wait_tasks timeout E2E tests passed!")
