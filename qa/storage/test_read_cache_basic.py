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

db = open_db(DB_PATH)

n = 5
for i in range(n):
    write_data(db, f"cache_{i}", i * 100)

assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=30.0), \
    f"Expected {n} writes to complete, got {len(master.completed_tasks)}"

for i in range(n):
    val = db.read_object(f"cache_{i}", cache="low")
    assert val == i * 100, f"Expected {i * 100}, got {val}"

for i in range(n):
    val = db.read_object(f"cache_{i}", cache="low")
    assert val == i * 100, f"LOW cache hit failed: expected {i * 100}, got {val}"

for i in range(n):
    val = db.read_object(f"cache_{i}", cache="high")
    assert val == i * 100, f"HIGH cache failed: expected {i * 100}, got {val}"

for i in range(n):
    val = db.read_object(f"cache_{i}", cache="high")
    assert val == i * 100, f"HIGH cache hit failed: expected {i * 100}, got {val}"

for i in range(n):
    val = db.read_object(f"cache_{i}", cache="none")
    assert val == i * 100, f"NONE cache failed: expected {i * 100}, got {val}"

INFO(f"[PASS] test_read_cache_basic: {n} objects, all cache modes verified")
