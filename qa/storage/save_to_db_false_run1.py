from _fly_log import INFO
import time
import os
import shutil



from test import write_data, write_temp
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

write_data(db, "perm", 42)
write_temp(db, "temp", 99)

assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0)
assert len(master.failed_tasks) == 0

assert db.read_object("perm") == 42
assert db.read_object("temp") == 99

INFO("[PASS] save_to_db_false_run1: wrote perm=42, temp=99")
