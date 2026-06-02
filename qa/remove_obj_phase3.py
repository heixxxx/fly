"""E2E: remove_object Phase 3 — remove one object, verify other still readable.
Runs as a separate fly binary process.
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_remove_obj_phase3_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, write_and_remove
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

write_data(db, "keep/obj", "keep_data")
write_data(db, "remove/obj", "remove_data")

completed = master.wait_for_all_tasks(expected=2, timeout=15)
assert len(completed) >= 2

write_and_remove(db, "remove/obj", "overwrite")
master.wait_for_all_tasks(expected=3, timeout=15)

keep_result = db.read_object("keep/obj")
assert keep_result == "keep_data", \
    f"Remaining object should still be readable, got: {keep_result}"

print("[PASS] test_remove_one_keeps_other: kept object still readable",
      file=sys.stderr)
