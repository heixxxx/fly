"""E2E test: Master spawns real subprocess workers via fly binary.

Tests the full Phase 3 flow:
  1. Master starts, auto-assigns port
  2. launch_local_workers() spawns fly --worker subprocesses
  3. Worker subprocesses connect back to Master
  4. Task submitted, assigned to worker, executed in subprocess
  5. TaskComplete returned to Master
"""
from _fly_log import INFO
import sys
import os
import time
import shutil

DB_PATH = f"/tmp/fly_e2e_process_workers_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config
from fly.runtime import get_agent


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


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([{}, {}])
assert wait_for(lambda: master.worker_count >= 2)

db = open_db(DB_PATH)

write_data(db, "key1", "value1")
write_data(db, "key2", "value2")
assert wait_for(lambda: len(master.completed_tasks) >= 2)

master.stop()
INFO("[PASS] test_e2e_process_workers")
