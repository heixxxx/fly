"""E2E: remove_object Phase 2 — dependent task fails after object removed.
Runs as a separate fly binary process.
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_remove_obj_phase2_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, write_and_remove, read_after_remove
from fly import open_db
from fly import get_config
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def main():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 1)

    master = get_agent()
    master.start()
    master.launch_local_workers([{}])
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
    print("[PASS] test_remove_then_dependent_task_fails: dependent task failed as expected",
          file=sys.stderr)


if __name__ == "__main__":
    main()
