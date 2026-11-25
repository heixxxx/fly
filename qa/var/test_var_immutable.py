import os
import shutil
from _fly_log import INFO
from fly import get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")

from fly import open_db, get_config, get_work_directory, as_task
from test import record, wait_tasks_no_failure


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
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 1)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 1, "first_set/no_failed_tasks", timeout=20.0)

set_var_expect_reject(db, "k", 2)
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 2)
# 原断言：assert len(master.failed_tasks) == 0, f"second set should be rejected, not fail task: {master.failed_tasks}"
assert wait_tasks_no_failure(master, 2, "second_set/no_failed_tasks", timeout=20.0)

# The original value must be preserved.
@as_task()
def assert_var(db, name, expected):
    # 任务函数内的断言保留：record 只能在主进程调用；断言失败时任务不计入
    # 已完成，由主进程的等待门槛（wait_tasks_no_failure）发现并给出失败详情。
    assert db.get_var(name) == expected, f"var {name} changed (immutable violated)"

assert_var(db, "k", 1)
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 3)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 3, "value_preserved/no_failed_tasks", timeout=20.0)

master.stop()
INFO("[DONE] test_var_immutable")
