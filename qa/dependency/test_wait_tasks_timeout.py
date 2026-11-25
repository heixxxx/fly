"""E2E test: wait_tasks timeout behavior.

Tests:
  1. wait_tasks(timeout=2.0) returns gracefully (no raise) when no tasks pending
  2. wait_tasks(timeout=1.0) with a submitted task returns partial results (no raise)
"""
from _fly_log import INFO
import time
import os
import shutil



from test import write_data
from test import record
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
    assert master.wait_workers_registered(timeout=60)
    # 原断言：assert master.worker_count >= 1, \
    record("no_pending/setup_worker_connected", master.worker_count >= 1,
           detail=master.worker_count)

    result = wait_tasks(timeout=2.0)
    # 原断言：assert result is not None, "wait_tasks should return a list, not None"
    record("no_pending/returns_list", result is not None)

    INFO("[DONE] test_wait_tasks_no_pending_returns_gracefully: "
         "wait_tasks returns without error when no tasks pending")


def test_wait_tasks_short_timeout_with_task():
    """wait_tasks(timeout=1.0) with a submitted task returns without raising."""
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    assert master.wait_workers_registered(timeout=60)
    # 原断言：assert master.worker_count >= 1, \
    record("short_timeout/setup_worker_connected", master.worker_count >= 1,
           detail=master.worker_count)

    db = open_db(DB_PATH)
    write_data(db, "timeout_key", 42)

    result = wait_tasks(timeout=1.0)
    # 原断言：assert result is not None, "wait_tasks should return a list, not None"
    record("short_timeout/returns_list", result is not None)

    INFO("[DONE] test_wait_tasks_short_timeout_with_task: "
         "wait_tasks returns without error even with short timeout")


test_wait_tasks_no_pending_returns_gracefully()
INFO("")
test_wait_tasks_short_timeout_with_task()
INFO("\nAll stages done!")
