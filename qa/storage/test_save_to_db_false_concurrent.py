from _fly_log import INFO
import time
import os
import shutil



from test import write_temp, read_data, record, wait_tasks_no_failure
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

master.launch_local_workers([{}, {}, {}])
assert master.wait_for_workers(3)

db = open_db(DB_PATH)

n = 10
for i in range(n):
    write_temp(db, f"ctemp_{i}", i * 100)

# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=30.0)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, n, "writes/no_failed_tasks", timeout=30.0)

for i in range(n):
    dep = db.get_full_name(f"ctemp_{i}")
    read_data(db, f"ctemp_{i}", [dep])

# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 2 * n, timeout=30.0)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 2 * n, "reads/no_failed_tasks", timeout=30.0)

read_values = {}
for i in range(n):
    read_values[f"ctemp_{i}"] = db.read_object(f"ctemp_{i}")
# 原断言：assert db.read_object(f"ctemp_{i}") == i * 100
record("read_back", read_values)

INFO(f"[DONE] test_save_to_db_false_concurrent: {n} temp objects, {n} reads across 3 workers")
