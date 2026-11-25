import os
from _fly_log import INFO

# Coordinator passes the shared db path via env var.
DB_PATH = os.environ["FLY_VAR_PERSISTENCE_DB"]

from fly import load_db, get_config, as_task
from test import record, wait_tasks_no_failure


get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)

# load_db restores the frozen db — vars are loaded from _VARS into master memory.
db = load_db(DB_PATH)


@as_task()
def assert_var(db, name, expected):
    # 任务函数内的断言保留：record 只能在主进程调用；断言失败时任务不计入
    # 已完成，由主进程的等待门槛（wait_tasks_no_failure）发现并给出失败详情。
    val = db.get_var(name)
    assert val == expected, f"var {name}={val!r}, expected {expected!r} (not persisted?)"


assert_var(db, "persist_int", 123)
assert_var(db, "persist_str", "hello")
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 2)
# 原断言：assert len(master.failed_tasks) == 0, f"persistence assertion failed: {master.failed_tasks}"
assert wait_tasks_no_failure(master, 2, "vars_persisted/no_failed_tasks", timeout=20.0)

master.stop()
INFO("[DONE] var_persistence_run2")
