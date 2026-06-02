"""E2E: remove_object Phase 1 — write_and_remove basic.
Runs as a separate fly binary process.
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_remove_obj_phase1_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_and_remove
from fly import open_db
from fly import get_config
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([{}])
for i in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1

db = open_db(DB_PATH)
write_and_remove(db, "temp/obj", "temp_data")

for i in range(40):
    completed = master.completed_tasks
    failed = master.failed_tasks
    if failed:
        err = master.get_task_error(failed[0])
        raise AssertionError(f"Task failed unexpectedly: {err}")
    if len(completed) >= 1:
        break
    time.sleep(0.5)

assert len(completed) >= 1

print("[PASS] test_remove_object_basic: write+remove completed", file=sys.stderr)
