from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_temp_concurrent_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                                '..', 'src'))

from e2e_tasks import write_temp, read_data
from fly import open_db, get_config


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

master.launch_local_workers([{}, {}, {}])
assert master.wait_for_workers(3)

db = open_db(DB_PATH)

n = 10
for i in range(n):
    write_temp(db, f"ctemp_{i}", i * 100)

assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=30.0)
assert len(master.failed_tasks) == 0

for i in range(n):
    dep = db.get_obj_name(f"ctemp_{i}")
    read_data(db, f"ctemp_{i}", [dep])

assert wait_for(lambda: len(master.completed_tasks) >= 2 * n, timeout=30.0)
assert len(master.failed_tasks) == 0

for i in range(n):
    assert db.read_object(f"ctemp_{i}") == i * 100

INFO(f"[PASS] test_save_to_db_false_concurrent: {n} temp objects, {n} reads across 3 workers")
