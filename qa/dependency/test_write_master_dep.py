from _fly_log import INFO
import time
import os
import shutil



from test import compute_sum
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


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

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

db.write_object("master_a", 10)
db.write_object("master_b", 20)

compute_sum(db, "master_a", "master_b", "sum_result")

assert wait_for(lambda: len(master.completed_tasks) >= 1), \
    f"compute_sum should complete, got {len(master.completed_tasks)} completed"

result = db.read_object("sum_result")
assert result == 30, f"Expected 30, got {result}"

INFO("[PASS] test_write_master_dep: Worker reads Master-written data via Tier 3")
