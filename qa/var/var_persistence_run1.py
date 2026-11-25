import os
from _fly_log import INFO

# Coordinator passes the shared db path via env var.
DB_PATH = os.environ["FLY_VAR_PERSISTENCE_DB"]

from fly import open_db, get_config, as_task
from test import record, wait_tasks_no_failure


get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)

db = open_db(DB_PATH)


@as_task()
def set_var(db, name, value):
    db.set_var(name, value)


@as_task()
def freeze_db(db):
    db.write_object("finish", 1)
    db.freeze()


set_var(db, "persist_int", 123)
set_var(db, "persist_str", "hello")
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 2)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 2, "set_vars/no_failed_tasks", timeout=20.0)

freeze_db(db)
# 原断言：assert wait_for(lambda: len(master.completed_tasks) >= 3)
# 原断言：assert len(master.failed_tasks) == 0
assert wait_tasks_no_failure(master, 3, "freeze/no_failed_tasks", timeout=20.0)

master.stop()
INFO("[DONE] var_persistence_run1")
