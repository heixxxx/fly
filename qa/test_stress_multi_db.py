"""E2E stress test: multiple databases operated in parallel.

Verifies 4 databases, each receiving 10 write tasks simultaneously
across 2 workers. All 40 tasks should complete without interference.
"""
import time
import sys
import os
import shutil

DB_PATHS = [f"/tmp/fly_e2e_stress_multidb_{i}" for i in range(4)]

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

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
    if not master._running:
        master.start()

    master.launch_local_workers([{}, {}])

    assert wait_for(lambda: master._agent.get_connection_count() >= 2), \
        f"Both workers should connect, got {master._agent.get_connection_count()}"

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

    for db in dbs:
        del db
    master.stop()
    print(f"[PASS] test_multi_db_parallel: 4 DBs x {writes_per_db} writes, all verified",
          file=sys.stderr)


if __name__ == "__main__":
    test_multi_db_parallel()
    print("\nAll tests passed!")
