from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_save_to_db_false_basic_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                                '..', 'src'))

from e2e_tasks import write_data, write_temp
from fly import open_db, get_config


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

assert wait_for(lambda: len(master.completed_tasks) >= 2, timeout=30.0)
assert len(master.failed_tasks) == 0

assert db.read_object("perm") == 100
assert db.read_object("temp") == 200

INFO("[PASS] test_save_to_db_false_basic")
