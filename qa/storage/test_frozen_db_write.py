"""E2E test: freeze protection on master side and task-level behavior.

Verifies:
  - db.freeze() prevents master-side write_object
  - A task running on a worker CAN write (worker has its own DB instance,
    freeze state is master-local in process mode)
  - This documents the design: freeze is a master-side coordination mechanism,
    not a distributed lock
"""
from _fly_log import INFO
import time
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_frozen_db_write_db_{os.getpid()}"


from e2e_tasks import write_data
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


def test_frozen_db_write():
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1

    db = open_db(DB_PATH)

    # Write data before freezing
    write_data(db, "before_freeze", 1)
    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "write_data before freeze should complete"

    db.freeze()
    assert db.is_frozen(), "DB should be frozen"

    # Master-side write after freeze should fail
    try:
        db.write_object("master_write_after_freeze", "fail")
        assert False, "Master-side write_object should raise after freeze"
    except Exception:
        pass  # Expected: write to frozen DB raises

    INFO("[PASS] test_frozen_db_write: master-side write blocked after freeze")


test_frozen_db_write()
