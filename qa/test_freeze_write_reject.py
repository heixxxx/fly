"""E2E test: Worker write rejected after freeze broadcast.

Verifies:
  - Worker A freezes DB during task execution
  - Master adds to frozen_dbs_ and broadcasts freeze notification
  - Worker B attempts write_after_freeze → write registration rejected by Master
  - Task fails with appropriate error
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_freeze_write_reject_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import freeze_db, write_after_freeze
from fly import open_db, get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_freeze_rejects_worker_write():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}, {}])

    assert wait_for(lambda: master._agent.get_connection_count() >= 2), \
        f"Both workers should connect, got {master._agent.get_connection_count()}"

    db = open_db(DB_PATH)

    # Phase 1: Worker A runs freeze_db — writes then freezes
    freeze_db(db, [])

    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "freeze_db task should complete"

    # Phase 2: Worker B attempts write_after_freeze
    write_after_freeze(db, "after_freeze_key", "value")

    assert wait_for(lambda: len(master.failed_tasks) >= 1), \
        f"write_after_freeze should fail (frozen DB), " \
        f"completed={len(master.completed_tasks)}, failed={len(master.failed_tasks)}"

    error = master.get_task_error(master.failed_tasks[0])
    assert "frozen" in error.lower(), f"Expected frozen error, got: {error}"

    del db
    master.stop()
    print(f"[PASS] test_freeze_rejects_worker_write: "
          f"Worker write rejected after freeze broadcast", file=sys.stderr)


if __name__ == "__main__":
    test_freeze_rejects_worker_write()
    print("\nAll tests passed!")
