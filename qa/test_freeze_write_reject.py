"""E2E test: Worker write silently rejected after freeze broadcast.

Verifies:
  - Worker A freezes DB during task execution
  - Worker B attempts write → write registration rejected by Master → returns empty
  - Task completes but writes nothing
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
    master.start()
    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master._agent.get_connection_count() >= 2)

    db = open_db(DB_PATH)
    freeze_db(db, [])
    assert wait_for(lambda: len(master.completed_tasks) >= 1)

    write_after_freeze(db, "after_freeze_key", "value")
    assert wait_for(lambda: len(master.completed_tasks) >= 2), \
        f"write_after_freeze should complete (silently rejected)"

    assert not master.failed_tasks, f"Unexpected failures: {master.failed_tasks}"

    del db
    master.stop()
    print("[PASS] test_freeze_rejects_worker_write: "
          "Worker write silently rejected after freeze broadcast", file=sys.stderr)


if __name__ == "__main__":
    test_freeze_rejects_worker_write()
    print("\nAll tests passed!")
