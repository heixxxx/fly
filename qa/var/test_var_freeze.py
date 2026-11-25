import os
import shutil
from _fly_log import INFO
from fly import get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")

from fly import open_db, get_config, get_work_directory, as_task
from test import record, wait_tasks


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)

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
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 1)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks(master, 1, "pre_freeze_set/no_failed_tasks", timeout=20.0)

# Freeze the db.
freeze_after_deps(db, [])
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 2)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks(master, 2, "freeze/no_failed_tasks", timeout=20.0)

# set_var after freeze must be rejected.
set_var_after_freeze_expect_reject(db, "post_freeze", 8)
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 3)
# 原断言：assert len(master.failed_tasks) == 0, f"set after freeze should be rejected: {master.failed_tasks}"
assert wait_tasks(master, 3, "post_freeze/no_failed_tasks", timeout=20.0)

# The pre-freeze var is still readable.
@as_task()
def assert_var(db, name, expected):
    # 任务函数内的断言保留：record 只能在主进程调用；断言失败时任务不计入
    # 已完成，由主进程的等待门槛（wait_tasks）发现并给出失败详情。
    assert db.get_var(name) == expected

assert_var(db, "pre_freeze", 7)
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 4)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks(master, 4, "pre_freeze_readable/no_failed_tasks", timeout=20.0)

master.stop()
INFO("[DONE] test_var_freeze")
