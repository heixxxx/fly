import os
import shutil
from _fly_log import INFO
from fly import get_work_directory

DB_PATH = os.path.join(get_work_directory(), "db")

from fly import open_db, get_config, as_task
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


# A var with a serialized payload > 1K (a large dict). set_var should succeed
# (the size warning is advisory) and the value must be retrievable intact.
@as_task()
def set_large_var(db, name, value):
    db.set_var(name, value)


@as_task()
def assert_var(db, name, expected):
    # 任务函数内的断言保留：record 只能在主进程调用；断言失败时任务不计入
    # 已完成，由主进程的等待门槛（wait_tasks_no_failure）发现并给出失败详情。
    val = db.get_var(name)
    assert val == expected, f"var {name} mismatch: got len={len(val) if val else None}, expected len={len(expected)}"


big_value = {"data": list(range(300))}  # pickle > 1K
set_large_var(db, "big", big_value)
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 1)
# 原断言：assert len(master.failed_tasks) == 0, f"set_large_var failed: {master.failed_tasks}"
assert wait_tasks_no_failure(master, 1, "set_large_var/no_failed_tasks", timeout=20.0)

assert_var(db, "big", big_value)
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 2)
# 原断言：assert len(master.failed_tasks) == 0, f"assert_var failed: {master.failed_tasks}"
assert wait_tasks_no_failure(master, 2, "big_var_intact/no_failed_tasks", timeout=20.0)

master.stop()
INFO("[DONE] test_var_too_large")
