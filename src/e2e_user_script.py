import time
import sys
import shutil
import os

from fly import open_db
from fly.runtime import get_agent
from e2e_tasks import write_data, freeze_db, read_data, write_after_freeze, fanout_write

DB_PATH = "/tmp/fly_e2e_db"


def wait_completed(master, expected, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        c = master.completed_tasks
        if len(c) >= expected:
            return c
        time.sleep(0.5)
    return master.completed_tasks


def setup_master():
    master = get_agent()
    if not master._running:
        master.start()
    master.launch_local_workers([{"role": "hybrid"}], mode="process")
    for i in range(20):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1, "Worker not connected"
    return master


def cleanup():
    for p in [DB_PATH, "fly_log"]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def run_all():
    cleanup()
    master = setup_master()

    db = open_db(DB_PATH)
    initial = len(master.completed_tasks)

    print("--- test_worker_db_write ---", file=sys.stderr)

    write_data(db, "x", 42)
    write_data(db, "y", "hello")

    completed = wait_completed(master, initial + 2)
    assert len(completed) - initial >= 2, f"Expected 2 completed, got {len(completed) - initial}"

    assert db.read_object("x") == 42
    assert db.read_object("y") == "hello"
    print(f"[PASS] test_worker_db_write", file=sys.stderr)
    print(file=sys.stderr)

    print("--- test_dependency_and_freeze ---", file=sys.stderr)
    base = len(master.completed_tasks)

    write_data(db, "data1", "value1")
    write_data(db, "data2", "value2")
    wait_completed(master, base + 2)

    db.read_object("data1")
    db.read_object("data2")
    obj1 = db.get_obj_name("data1")
    obj2 = db.get_obj_name("data2")

    freeze_db(db, [obj1, obj2])

    completed = wait_completed(master, base + 3, timeout=20)
    assert len(completed) - base >= 3, f"Expected 3 tasks, got {len(completed) - base}"

    assert db.is_frozen(), "DB should be frozen after freeze_db task"
    assert db.read_object("finish") == 1, "finish marker should be 1"
    print(f"[PASS] test_dependency_and_freeze", file=sys.stderr)
    print(file=sys.stderr)

    print("--- test_read_frozen_db ---", file=sys.stderr)
    base = len(master.completed_tasks)

    db2 = open_db(DB_PATH + "_frozen")
    write_data(db2, "frozen_data", "can_read")
    wait_completed(master, base + 1)

    obj_name = db2.get_obj_name("frozen_data")
    freeze_db(db2, [obj_name])
    wait_completed(master, base + 2)
    assert db2.is_frozen()

    finish_obj = db2.get_obj_name("finish")
    read_data(db2, "frozen_data", [finish_obj])

    completed = wait_completed(master, base + 3, timeout=15)
    assert len(completed) - base >= 3, "Read from frozen db task should complete"
    print(f"[PASS] test_read_frozen_db", file=sys.stderr)
    print(file=sys.stderr)

    print("--- test_write_frozen_db_fails ---", file=sys.stderr)
    base = len(master.completed_tasks)

    db3 = open_db(DB_PATH + "_blocked")
    write_data(db3, "pre", "exists")
    wait_completed(master, base + 1)

    freeze_db(db3, [db3.get_obj_name("pre")])
    wait_completed(master, base + 2)
    assert db3.is_frozen()

    write_after_freeze(db3, "blocked", "nope")

    time.sleep(5)

    assert db3.read_object("pre") == "exists"
    print(f"[PASS] test_write_frozen_db_fails", file=sys.stderr)
    print(file=sys.stderr)

    print("--- test_recursive_submit ---", file=sys.stderr)
    base = len(master.completed_tasks)

    db4 = open_db(DB_PATH + "_fanout")
    fanout_write(db4, ["a", "b", "c"], [10, 20, 30])

    completed = wait_completed(master, base + 4, timeout=30)
    new_count = len(completed) - base
    assert new_count >= 4, f"Expected 4 tasks, got {new_count}"
    print(f"[PASS] test_recursive_submit: {new_count} tasks completed", file=sys.stderr)

    for k, v in [("a", 10), ("b", 20), ("c", 30)]:
        assert db4.read_object(k) == v, f"Expected {v} for {k}, got {db4.read_object(k)}"
    print(f"  Verified db reads: a=10, b=20, c=30", file=sys.stderr)

    del db, db2, db3, db4
    master.stop()


if __name__ == "__main__":
    run_all()
    print("\n=== All E2E tests passed ===", file=sys.stderr)
