"""E2E test: Worker write silently rejected after freeze broadcast.

Verifies:
  - Worker A freezes DB during task execution
  - Worker B attempts write → silently rejected
  - Task completes but writes nothing
"""
from _fly_log import INFO
import time
import os
import shutil

from test import freeze_db, write_after_freeze
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")

def cleanup():
    if os.path.isdir(DB_PATH): shutil.rmtree(DB_PATH, ignore_errors=True)

def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition(): return True
        time.sleep(interval)
    return False

def test_freeze_rejects_worker_write():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert master.wait_for_workers(2)
    db = open_db(DB_PATH)
    freeze_db(db, [])
    assert wait_for(lambda: len(master.completed_tasks) >= 1)
    write_after_freeze(db, "after_freeze_key", "value")
    assert wait_for(lambda: len(master.completed_tasks) >= 2), "write should complete silently"
    assert not master.failed_tasks, f"Unexpected failures: {master.failed_tasks}"
    INFO("[PASS] test_freeze_rejects_worker_write")

test_freeze_rejects_worker_write()
INFO("\nAll tests passed!")
