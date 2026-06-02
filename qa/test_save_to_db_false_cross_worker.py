import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_temp_cross_worker_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_temp, read_data
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

master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2)

db = open_db(DB_PATH)

write_temp(db, "shared_temp", 777)

assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0)
assert len(master.failed_tasks) == 0

dep = db.get_obj_name("shared_temp")
read_data(db, "shared_temp", [dep])
read_data(db, "shared_temp", [dep])

assert wait_for(lambda: len(master.completed_tasks) >= 3, timeout=30.0)
assert len(master.failed_tasks) == 0

assert db.read_object("shared_temp") == 777

print("[PASS] test_save_to_db_false_cross_worker", file=sys.stderr)
