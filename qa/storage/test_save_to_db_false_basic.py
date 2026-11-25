from _fly_log import INFO
import time
import os
import shutil



from test import write_data, write_temp, record, wait_tasks
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

write_data(db, "perm", 100)
write_temp(db, "temp", 200)

# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks(master, 2, "writes/no_failed_tasks", timeout=30.0)

# 原断言：assert db.read_object("perm") == 100
record("read_perm", db.read_object("perm"))
# 原断言：assert db.read_object("temp") == 200
record("read_temp", db.read_object("temp"))

INFO("[DONE] test_save_to_db_false_basic")
