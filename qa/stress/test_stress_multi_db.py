"""E2E stress test: multiple databases operated in parallel.

Verifies 4 databases, each receiving 10 write tasks simultaneously
across 2 workers. All 40 tasks should complete without interference.
"""
from _fly_log import INFO
import time
import os
import shutil

DB_PATHS = [f"/tmp/fly_e2e_stress_multidb_{i}" for i in range(4)]


from e2e_tasks import write_data
from fly import open_db, get_config


def cleanup():
    for p in DB_PATHS:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_multi_db_parallel():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    dbs = [open_db(p) for p in DB_PATHS]
    writes_per_db = 10

    for db_idx, db in enumerate(dbs):
        for i in range(writes_per_db):
            write_data(db, f"db{db_idx}_key_{i}", db_idx * 100 + i)

    total = 4 * writes_per_db
    assert wait_for(lambda: len(master.completed_tasks) >= total, timeout=60.0), \
        f"All {total} tasks should complete, got {len(master.completed_tasks)} completed"

    assert len(master.completed_tasks) >= total, \
        f"Expected >= {total} completed, got {len(master.completed_tasks)}"
    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    for db_idx, db in enumerate(dbs):
        for i in range(writes_per_db):
            val = db.read_object(f"db{db_idx}_key_{i}")
            assert val == db_idx * 100 + i, \
                f"db{db_idx}_key_{i} should be {db_idx * 100 + i}, got {val}"

    INFO(f"[PASS] test_multi_db_parallel: 4 DBs x {writes_per_db} writes, all verified")


test_multi_db_parallel()
INFO("\nAll tests passed!")
