import os
from _fly_log import INFO

# Coordinator passes the shared db path via env var.
DB_PATH = os.environ["FLY_VAR_PERSISTENCE_DB"]

from fly import load_db, get_config, as_task


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

# load_db restores the frozen db — vars are loaded from _VARS into master memory.
db = load_db(DB_PATH)


@as_task()
def assert_var(db, name, expected):
    val = db.get_var(name)
    assert val == expected, f"var {name}={val!r}, expected {expected!r} (not persisted?)"


assert_var(db, "persist_int", 123)
assert_var(db, "persist_str", "hello")
assert wait_for(lambda: len(master.completed_tasks) >= 2)
assert len(master.failed_tasks) == 0, f"persistence assertion failed: {master.failed_tasks}"

master.stop()
INFO("[PASS] var_persistence_run2")
