"""Phase 2: Worker writes via @as_task → @wait_obj blocks → reads and verifies."""
from _fly_log import INFO
import time
import os
import shutil

DB_PATH = "/tmp/fly_e2e_wait_obj_p2_db"

from e2e_tasks import write_data
from fly import open_db, get_config, wait_obj
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
for i in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1

db = open_db(DB_PATH)

@wait_obj(inputs=lambda d, k: [d.get_obj_name(k)])
def wait_then_read(d, k):
    return d.read_object(k)

write_data(db, "greeting", "hello world")
result = wait_then_read(db, "greeting")
assert result == "hello world"

assert wait_for(lambda: len(master.completed_tasks) >= 1)

INFO("[PASS] test_wait_obj_worker_write")
