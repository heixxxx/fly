import os
import shutil
from _fly_log import INFO
from fly import get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")

from fly import open_db, get_config, as_task


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    import time
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
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)


# A var with a serialized payload > 1K (a large dict). set_var should succeed
# (the size warning is advisory) and the value must be retrievable intact.
@as_task()
def set_large_var(db, name, value):
    db.set_var(name, value)


@as_task()
def assert_var(db, name, expected):
    val = db.get_var(name)
    assert val == expected, f"var {name} mismatch: got len={len(val) if val else None}, expected len={len(expected)}"


big_value = {"data": list(range(300))}  # pickle > 1K
set_large_var(db, "big", big_value)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0, f"set_large_var failed: {master.failed_tasks}"

assert_var(db, "big", big_value)
assert wait_for(lambda: len(master.completed_tasks) >= 2)
assert len(master.failed_tasks) == 0, f"assert_var failed: {master.failed_tasks}"

master.stop()
INFO("[PASS] test_var_too_large")
