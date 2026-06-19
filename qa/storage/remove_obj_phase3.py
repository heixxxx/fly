"""E2E: remove_object Phase 3 — remove one object, verify other still readable.
Runs as a separate fly binary process.
"""
from _fly_log import INFO
import time
import os
import shutil



from e2e_tasks import write_data, write_and_remove
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
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

INFO("[PASS] test_remove_one_keeps_other: kept object still readable")
