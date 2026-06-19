from _fly_log import INFO
import time
import os
import shutil



from e2e_tasks import write_data
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

db1_path = DB_PATH + "_db1"
db2_path = DB_PATH + "_db2"
if os.path.isdir(db1_path):
    shutil.rmtree(db1_path, ignore_errors=True)
if os.path.isdir(db2_path):
    shutil.rmtree(db2_path, ignore_errors=True)

db1 = open_db(db1_path)
db2 = open_db(db2_path)

write_data(db1, "shared_key", 42)
write_data(db2, "shared_key", 99)

assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0)

val1 = db1.read_object("shared_key", cache="low")
val2 = db2.read_object("shared_key", cache="low")
assert val1 == 42, f"db1 expected 42, got {val1}"
assert val2 == 99, f"db2 expected 99, got {val2}"

val1 = db1.read_object("shared_key", cache="high")
val2 = db2.read_object("shared_key", cache="high")
assert val1 == 42, f"db1 HIGH expected 42, got {val1}"
assert val2 == 99, f"db2 HIGH expected 99, got {val2}"

INFO(f"[PASS] test_read_cache_cross_db: cross-DB cache isolation verified")
