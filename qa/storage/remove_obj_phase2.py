"""E2E: remove_object Phase 2 — dependent task fails after object removed.
Runs as a separate fly binary process.
"""
from _fly_log import INFO
import time
import os
import shutil



from test import write_data, write_and_remove, read_after_remove
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 1)

master = get_agent()
master.launch_local_workers([{}])
for i in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1

db = open_db(DB_PATH)

write_data(db, "will_remove", 42)
time.sleep(1)

completed = master.wait_for_all_tasks(expected=1, timeout=15)
assert len(completed) >= 1

write_and_remove(db, "will_remove", 99)
master.wait_for_all_tasks(expected=2, timeout=15)

removed_full = db.get_full_name("will_remove")
read_after_remove(db, "result", [removed_full])

for i in range(40):
    failed = master.failed_tasks
    if failed:
        break
    time.sleep(0.5)

assert len(failed) >= 1, \
    f"Task depending on removed object should fail, got failed={failed}"

INFO("[PASS] test_remove_then_dependent_task_fails: dependent task failed as expected")
