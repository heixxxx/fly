"""E2E test: remove_object deletes index and makes object unreadable.

Phase 1: Write + remove on same Worker, verify read fails
Phase 2: Write data, task removes it, downstream task depending on removed data should fail
Phase 3: Write two objects, remove one, verify other still readable
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_remove_obj_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, write_and_remove, read_after_remove
from fly import open_db
from fly import get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_remove_object_basic():
    """Phase 1: write_and_remove writes then removes, read should fail on Worker."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()
    master.launch_local_workers([{}], mode="process")
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    write_and_remove(db, "temp/obj", "temp_data")

    for i in range(40):
        completed = master.completed_tasks
        failed = master.failed_tasks
        if failed:
            err = master.get_task_error(failed[0])
            master.stop()
            raise AssertionError(f"Task failed unexpectedly: {err}")
        if len(completed) >= 1:
            break
        time.sleep(0.5)

    assert len(completed) >= 1

    master.stop()
    print(f"[PASS] test_remove_object_basic: write+remove completed", file=sys.stderr)


def test_remove_then_dependent_task_fails():
    """Phase 2: write data, remove it, then submit a task that depends on the removed object.
    The dependent task should fail because the data index is gone."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()
    master.launch_local_workers([{}], mode="process")
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    write_data(db, "will_remove", 42)
    time.sleep(1)

    completed = master.wait_for_all_tasks(expected=1, timeout=15)
    assert len(completed) >= 1

    from e2e_tasks import write_and_remove
    write_and_remove(db, "will_remove", 99)

    master.wait_for_all_tasks(expected=2, timeout=15)

    removed_full = db.get_obj_name("will_remove")

    read_after_remove(db, "result", [removed_full])

    for i in range(40):
        failed = master.failed_tasks
        if failed:
            break
        time.sleep(0.5)

    assert len(failed) >= 1, \
        f"Task depending on removed object should fail, got failed={failed}"

    master.stop()
    print(f"[PASS] test_remove_then_dependent_task_fails: dependent task failed as expected", file=sys.stderr)


def test_remove_one_keeps_other():
    """Phase 3: write two objects, remove one via task, verify the other is still readable."""
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()
    master.launch_local_workers([{}], mode="process")
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    write_data(db, "keep/obj", "keep_data")
    write_data(db, "remove/obj", "remove_data")

    completed = master.wait_for_all_tasks(expected=2, timeout=15)
    assert len(completed) >= 2

    from e2e_tasks import write_and_remove
    write_and_remove(db, "remove/obj", "overwrite")

    master.wait_for_all_tasks(expected=3, timeout=15)

    keep_result = db.read_object("keep/obj")
    assert keep_result == "keep_data", \
        f"Remaining object should still be readable, got: {keep_result}"

    master.stop()
    print(f"[PASS] test_remove_one_keeps_other: kept object still readable", file=sys.stderr)


if __name__ == "__main__":
    test_remove_object_basic()
    test_remove_then_dependent_task_fails()
    test_remove_one_keeps_other()
    print("\nAll remove_object E2E tests passed!")