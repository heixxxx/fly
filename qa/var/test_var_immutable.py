import os
import shutil
from _fly_log import INFO
from fly import get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")

from fly import open_db, get_config, get_work_directory, as_task


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


# First set succeeds.
@as_task()
def set_var(db, name, value):
    db.set_var(name, value)


# Second set on the same name must be rejected (immutable). The task catches
# the RuntimeError so it still completes (does not fail).
@as_task()
def set_var_expect_reject(db, name, value):
    try:
        db.set_var(name, value)
        raise AssertionError(f"set_var('{name}') should have been rejected (immutable)")
    except RuntimeError:
        pass  # expected


set_var(db, "k", 1)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

set_var_expect_reject(db, "k", 2)
assert wait_for(lambda: len(master.completed_tasks) >= 2)
assert len(master.failed_tasks) == 0, f"second set should be rejected, not fail task: {master.failed_tasks}"

# The original value must be preserved.
@as_task()
def assert_var(db, name, expected):
    assert db.get_var(name) == expected, f"var {name} changed (immutable violated)"

assert_var(db, "k", 1)
assert wait_for(lambda: len(master.completed_tasks) >= 3)
assert len(master.failed_tasks) == 0

master.stop()
INFO("[PASS] test_var_immutable")
