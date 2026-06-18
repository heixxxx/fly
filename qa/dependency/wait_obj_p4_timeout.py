"""Phase 4: @wait_obj raises RuntimeError when data can never appear."""
from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_wait_obj_p4_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config, wait_obj
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


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
def wait_phantom(d, k):
    return d.read_object(k)

try:
    wait_phantom(db, "phantom")
    assert False, "Should have raised RuntimeError"
except RuntimeError as e:
    assert "cannot be produced" in str(e)
except Exception as e:
    assert False, f"Expected RuntimeError, got {type(e).__name__}: {e}"

INFO("[PASS] test_wait_obj_timeout")
