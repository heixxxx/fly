"""Phase 3: Two Worker writes → @wait_obj waits for both → executes and returns result."""
from _fly_log import INFO
import time
import os
import shutil


from e2e_tasks import write_data
from fly import open_db, get_config, wait_obj
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
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

@wait_obj(inputs=lambda d, a, b: [d.get_obj_name(a), d.get_obj_name(b)])
def wait_and_sum(d, key_a, key_b):
    return d.read_object(key_a) + d.read_object(key_b)

write_data(db, "x", 100)
write_data(db, "y", 200)

result = wait_and_sum(db, "x", "y")
assert result == 300

assert wait_for(lambda: len(master.completed_tasks) >= 2)

INFO("[PASS] test_wait_obj_multi_deps")
