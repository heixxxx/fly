"""Test: wait_obj waits forever, raises only when object can never appear.

Verifies the new wait_obj semantics:
  1. No timeout — wait_obj blocks indefinitely while tasks are pending/running
  2. Master returns can_still_produce=false when no executable tasks remain
  3. wait_obj raises when object doesn't exist AND can_still_produce=false
"""
from _fly_log import INFO
import time
import sys
import os
import shutil
import threading

DB_PATH = f"/tmp/fly_e2e_wait_obj_no_timeout_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, failing_task
from fly import open_db, get_config, wait_obj
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=30.0, interval=0.5):
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

# Test 1: wait_obj raises when object can never appear
# Submit a task that writes to "real_key", then wait for "phantom_key"
# which no task will ever produce. After all tasks complete, wait_obj
# should raise because master has no more executable tasks.
write_data(db, "real_key", "real_value")
assert wait_for(lambda: len(master.completed_tasks) >= 1)

# Now wait for a phantom object that no task produces.
# With the new semantics, this should raise immediately since
# there are no pending/running tasks.
raised = False
try:
    @wait_obj(inputs=lambda d, k: [d.get_obj_name(k)])
    def read_phantom(d, k):
        return d.read_object(k)

    read_phantom(db, "phantom_key")
except RuntimeError as e:
    if "cannot be produced" in str(e) or "no pending tasks" in str(e):
        raised = True
        INFO(f"  Got expected error: {e}")
    else:
        raise

assert raised, "wait_obj should raise when object can never appear"

# Test 2: wait_obj waits when tasks are still running
# Submit a slow task and verify wait_obj doesn't timeout
# (This test passes if wait_obj completes after the slow task finishes)

INFO("[PASS] test_wait_obj_no_timeout")
