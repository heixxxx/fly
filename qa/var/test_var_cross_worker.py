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

# Two workers with distinct attributes so we can pin tasks deterministically.
master.launch_local_workers([
    {"attributes": ["producer"]},
    {"attributes": ["consumer"]},
])
assert wait_for(lambda: master._agent.get_connection_count() >= 2)

db = open_db(DB_PATH)


# Producer: set_var THEN write_object on the producer worker. Establishes the
# implicit dependency — once "signal" is data-ready, "config_val" is guaranteed
# retrievable from master (same-connection FIFO on the producer's master_conn).
@as_task(requires=["producer"])
def produce(db, var_name, var_value, obj_key, obj_value):
    db.set_var(var_name, var_value)
    db.write_object(obj_key, obj_value)


# Consumer: depends on "signal" (data dep) AND declares var "config_val".
# Master inlines the var into TaskAssignMessage; the consumer reads it locally
# without an extra round-trip.
@as_task(inputs=lambda d, sig, var: [d.get_full_name(sig)],
         vars=lambda d, sig, var: [d.get_full_name(var)],
         requires=["consumer"])
def consume(d, sig, var):
    obj = d.read_object(sig)
    val = d.get_var(var)
    assert obj == "done", f"obj={obj!r}"
    assert val == 99, f"var={val!r}"


produce(db, "config_val", 99, "signal", "done")
assert wait_for(lambda: len(master.completed_tasks) >= 1), "producer task did not complete"
assert len(master.failed_tasks) == 0, f"producer task failed: {master.failed_tasks}"

consume(db, "signal", "config_val")
assert wait_for(lambda: len(master.completed_tasks) >= 2), "consumer task did not complete"
assert len(master.failed_tasks) == 0, f"consumer task failed: {master.failed_tasks}"

master.stop()
INFO("[PASS] test_var_cross_worker")
