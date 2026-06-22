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
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

db = open_db(DB_PATH)


@as_task()
def set_var(db, name, value):
    db.set_var(name, value)


@as_task(inputs=lambda d, deps: list(deps))
def freeze_after_deps(d, deps):
    d.write_object("finish", 1)
    d.freeze()


@as_task()
def set_var_after_freeze_expect_reject(db, name, value):
    try:
        db.set_var(name, value)
        raise AssertionError(f"set_var('{name}') after freeze should be rejected")
    except RuntimeError:
        pass  # expected: var is db data, freeze makes it immutable too


# Set a var before freeze.
set_var(db, "pre_freeze", 7)
assert wait_for(lambda: len(master.completed_tasks) >= 1)
assert len(master.failed_tasks) == 0

# Freeze the db.
freeze_after_deps(db, [])
assert wait_for(lambda: len(master.completed_tasks) >= 2)
assert len(master.failed_tasks) == 0

# set_var after freeze must be rejected.
set_var_after_freeze_expect_reject(db, "post_freeze", 8)
assert wait_for(lambda: len(master.completed_tasks) >= 3)
assert len(master.failed_tasks) == 0, f"set after freeze should be rejected: {master.failed_tasks}"

# The pre-freeze var is still readable.
@as_task()
def assert_var(db, name, expected):
    assert db.get_var(name) == expected

assert_var(db, "pre_freeze", 7)
assert wait_for(lambda: len(master.completed_tasks) >= 4)
assert len(master.failed_tasks) == 0

master.stop()
INFO(f"[PASS] test_var_freeze")
