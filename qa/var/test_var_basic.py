import os
import shutil
from _fly_log import INFO
from fly import get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")

from test import set_var_task, get_var_task, remove_var_task
from fly import open_db, get_config, get_work_directory


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
assert master.wait_workers_registered(timeout=60)

db = open_db(DB_PATH)

# Basic set/get cycle (Python int).
set_var_task(db, "counter", 42)
assert wait_for(lambda: len(master.completed_tasks) >= 1), "set_var task did not complete"

get_var_task(db, "counter")
assert wait_for(lambda: len(master.completed_tasks) >= 2), "get_var task did not complete"

# The get_var_task result is captured in the task completion; verify via a
# direct get_var_task that returns the value through task output.
# Since task return values aren't directly queryable, re-run and assert via
# a task that asserts internally.
from fly import as_task

@as_task()
def assert_var_equals(db, name, expected):
    val = db.get_var(name)
    assert val == expected, f"var {name}={val!r}, expected {expected!r}"

assert_var_equals(db, "counter", 42)
assert wait_for(lambda: len(master.completed_tasks) >= 3), "assert task did not complete"
assert len(master.failed_tasks) == 0, f"assert task failed: {master.failed_tasks}"

# Python str and dict.
set_var_task(db, "greeting", "hello")
set_var_task(db, "config", {"a": 1, "b": [2, 3]})
assert wait_for(lambda: len(master.completed_tasks) >= 5)
assert_var_equals(db, "greeting", "hello")
assert_var_equals(db, "config", {"a": 1, "b": [2, 3]})
assert wait_for(lambda: len(master.completed_tasks) >= 7)
assert len(master.failed_tasks) == 0

# get_var on a missing name returns None (default).
@as_task()
def assert_var_missing(db, name):
    val = db.get_var(name)
    assert val is None, f"expected None for missing var, got {val!r}"

assert_var_missing(db, "does_not_exist")
assert wait_for(lambda: len(master.completed_tasks) >= 8)
assert len(master.failed_tasks) == 0

# remove_var then get_var returns None.
remove_var_task(db, "counter")
assert wait_for(lambda: len(master.completed_tasks) >= 9)
assert_var_missing(db, "counter")
assert wait_for(lambda: len(master.completed_tasks) >= 10)
assert len(master.failed_tasks) == 0

master.stop()
INFO("[PASS] test_var_basic")
