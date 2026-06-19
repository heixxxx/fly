"""Phase 5: Cross-Worker @wait_obj — Worker B polls for Worker A's output.

Launches 2 Workers so tasks may land on different Workers.
If write_data runs on Worker 1 and wait_obj_then_process on Worker 2,
@wait_obj in Worker 2 must use Tier 3 probe (→ Master query → update_remote_idx)
to discover Worker 1's data.
"""
from _fly_log import INFO
import time
import os
import shutil

DB_PATH = "/tmp/fly_e2e_wait_obj_p5_db"

from e2e_tasks import write_data, wait_obj_then_process
from fly import open_db, get_config
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
# Two Workers — tasks may be distributed across them
master.launch_local_workers([{}, {}])
for i in range(40):
    if master.worker_count >= 2:
        break
    time.sleep(0.5)
assert master.worker_count >= 2

db = open_db(DB_PATH)

# Step 1: Worker A (or B) writes upstream data
write_data(db, "hello_upstream", "hello worker")
assert wait_for(lambda: len(master.completed_tasks) >= 1)

# Step 2: Worker B (or A) uses @wait_obj to wait for upstream
# If on different Worker: Tier 3 probe triggers, updates remote_idx
wait_obj_then_process(db, "hello_upstream", "processed_result")
assert wait_for(lambda: len(master.completed_tasks) >= 2)

result = db.read_object("processed_result")
assert result == "processed:hello worker"

INFO("[PASS] test_wait_obj_inside_worker_task (2 workers)")
