"""E2E test: fly public API — open_db, as_task, task_name, get_agent."""
from _fly_log import INFO
import os
import time
import shutil



from test import write_data
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
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
master.launch_local_workers([{}])
assert wait_for(lambda: master.worker_count >= 1)

db = open_db(DB_PATH)
obj_name = db.get_full_name("output/result")
assert ":" in obj_name
assert obj_name.endswith(":output/result")

write_data(db, "test_key", "test_value")
assert wait_for(lambda: len(master.completed_tasks) >= 1)

master.stop()
INFO("[PASS] test_e2e_fly_api")
