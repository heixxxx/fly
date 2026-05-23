"""Run 1 of two-process load_db test.
Creates DB, writes data via tasks, does NOT freeze.
"""
import os
import sys
import time

DB_PATH = "/tmp/fly_e2e_load_db_twoproc"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db
from fly.config import get_config
from fly.runtime import get_agent


def main():
    # Cleanup previous run
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

    get_config().set_int("fail_unscheduleable_tasks", 0)

    from _fly_storage import ex_stg_get_data_service

    master = get_agent()
    master.start()

    # Launch 1 process worker for task execution
    master.launch_local_workers([{}], mode="process")

    # Wait for worker
    for _ in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1, "Worker should connect"

    db = open_db(DB_PATH)
    db_id = db.get_db_id()

    # Submit tasks that write via worker
    write_data(db, "stage1/alpha", 100)
    write_data(db, "stage1/beta", 200)
    write_data(db, "stage1/gamma", "hello")

    # Wait for completion
    completed = master.wait_for_all_tasks(expected=3, timeout=30)
    assert len(completed) >= 3, f"Expected 3 completed tasks, got {len(completed)}"

    # Master also writes directly
    db.write_object("stage1/master_only", "from_master")
    db.write_object("stage1/config", {"version": 1, "source": "run1"})

    # Wait for write-back
    ex_stg_get_data_service().drain_write_back()
    time.sleep(0.5)

    # Verify data readable before exit
    assert db.read_object("stage1/master_only") == "from_master"
    assert db.read_object("stage1/config") == {"version": 1, "source": "run1"}

    # Do NOT freeze — that's Run 2's job

    # Print db_id for Run 2 to verify (write to a marker file)
    with open(os.path.join(DB_PATH, "_test_db_id"), "w") as f:
        f.write(db_id)

    # Verify _DB_META exists
    assert os.path.isfile(os.path.join(DB_PATH, "_DB_META")), "_DB_META should exist"

    print(f"[RUN1] Created DB: db_id={db_id}, path={DB_PATH}", file=sys.stderr)
    print(f"[RUN1] Wrote 5 objects (3 via worker, 2 via master)", file=sys.stderr)
    print(f"[RUN1] NOT freezing — exiting for Run 2 to load_db", file=sys.stderr)


if __name__ == "__main__":
    main()
