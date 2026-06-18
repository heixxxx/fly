from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_write_worker_dep_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, compute_sum
from fly import open_db, get_config


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

write_data(db, "input_a", 10)
write_data(db, "input_b", 20)

assert wait_for(lambda: len(master.completed_tasks) >= 2), \
    "Phase 1: both writes should complete"

compute_sum(db, "input_a", "input_b", "sum_result")

assert wait_for(lambda: len(master.completed_tasks) >= 3), \
    f"Phase 2: compute_sum should complete, got {len(master.completed_tasks)} completed"

result = db.read_object("sum_result")
assert result == 30, f"Expected 30, got {result}"

INFO("[PASS] test_write_worker_dep: data written by tasks satisfies downstream compute_sum")
