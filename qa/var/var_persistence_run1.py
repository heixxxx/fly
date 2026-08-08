import os
from _fly_log import INFO

# Coordinator passes the shared db path via env var.
DB_PATH = os.environ["FLY_VAR_PERSISTENCE_DB"]

from fly import open_db, get_config, as_task


def wait_for(condition, timeout=20.0, interval=0.5):
    import time
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert wait_for(lambda: master._agent.get_connection_count() >= 1)

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
assert wait_for(lambda: len(master.completed_tasks) >= 2)
assert len(master.failed_tasks) == 0

freeze_db(db)
assert wait_for(lambda: len(master.completed_tasks) >= 3)
assert len(master.failed_tasks) == 0

master.stop()
INFO("[PASS] var_persistence_run1")
