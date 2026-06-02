import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_user_script_task_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import as_task, open_db


@as_task()
def user_write(db, key, value):
    db.write_object(key, value)


@as_task(inputs=lambda db, dep_key, result_key: [db.get_obj_name(dep_key)])
def user_read_after_write(db, dep_key, result_key):
    val = db.read_object(dep_key)
    db.write_object(result_key, f"echo:{val}")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def setup_workers():
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    for _ in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1, \
        f"Only {master.worker_count}/1 workers connected"
    return master


def wait_completed(master, expected, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        c = master.completed_tasks
        if len(c) >= expected:
            return c
        failed = master.failed_tasks
        if failed:
            raise RuntimeError(f"Tasks failed: {failed}")
        time.sleep(0.5)
    return master.completed_tasks


def test_user_script_task():
    cleanup()
    master = setup_workers()
    db = open_db(DB_PATH)

    user_write(db, "ukey", 42)
    user_read_after_write(db, "ukey", "uresult")

    completed = wait_completed(master, 2, timeout=30)
    assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"

    assert db.read_object("ukey") == 42
    assert db.read_object("uresult") == "echo:42"

    print(f"[PASS] test_user_script_task: {len(completed)} tasks completed",
          file=sys.stderr)


test_user_script_task()
