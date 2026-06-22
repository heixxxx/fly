"""Phase 4: @wait_obj raises RuntimeError when data can never appear."""
from _fly_log import INFO
import time
import os
import shutil


from fly import open_db, get_config, wait_obj
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
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

@wait_obj(inputs=lambda d, k: [d.get_full_name(k)])
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
