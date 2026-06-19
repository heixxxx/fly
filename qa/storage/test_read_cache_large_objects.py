from _fly_log import INFO
import time
import os
import shutil



from e2e_tasks import write_data
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


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

db = open_db(DB_PATH + "_large")

large_data = list(range(10000))
write_data(db, "large_obj", large_data)

assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0)

val = db.read_object("large_obj", cache="low")
assert val == large_data, "Large object read failed"

val = db.read_object("large_obj", cache="high")
assert val == large_data, "Large object HIGH cache failed"

val = db.read_object("large_obj", cache="high")
assert val == large_data, "Large object HIGH cache hit failed"

INFO(f"[PASS] test_read_cache_large_objects: large object caching verified")
