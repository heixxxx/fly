"""E2E stress test: cross-DB data transfer at scale.

Verifies 3 databases with cross-DB dependency chains:
  DB_A writes raw data -> DB_B reads from DB_A and writes features -> DB_C reads from both and writes result
"""
from _fly_log import INFO
import time
import os
import shutil

DB_A = "/tmp/fly_e2e_stress_crossdb_a"
DB_B = "/tmp/fly_e2e_stress_crossdb_b"
DB_C = "/tmp/fly_e2e_stress_crossdb_c"


from e2e_tasks import write_data, cross_db_copy, triple_db_sum
from fly import open_db, get_config


def cleanup():
    for p in [DB_A, DB_B, DB_C]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_cross_db_transfer():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    db_a = open_db(DB_A)
    db_b = open_db(DB_B)
    db_c = open_db(DB_C)

    n = 10

    for i in range(n):
        write_data(db_a, f"raw_{i}", i * 100)

    assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=60.0), \
        f"Phase 1: {n} raw writes should complete, got {len(master.completed_tasks)}"

    for i in range(n):
        cross_db_copy(db_b, db_a, f"raw_{i}", f"feat_{i}")

    assert wait_for(lambda: len(master.completed_tasks) >= 2 * n, timeout=60.0), \
        f"Phase 2: {2*n} total should complete, got {len(master.completed_tasks)}"

    for i in range(n):
        triple_db_sum(db_c, db_a, db_b, f"raw_{i}", f"feat_{i}", f"result_{i}")

    total_expected = 3 * n
    assert wait_for(lambda: len(master.completed_tasks) >= total_expected, timeout=60.0), \
        f"Phase 3: {total_expected} total should complete, got {len(master.completed_tasks)}"

    assert len(master.completed_tasks) >= total_expected, \
        f"Expected >= {total_expected} completed, got {len(master.completed_tasks)}"
    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    for i in range(n):
        result = db_c.read_object(f"result_{i}")
        assert result == i * 100 + i * 100, \
            f"result_{i} should be {i * 200}, got {result}"

    INFO(f"[PASS] test_cross_db_transfer: 3 DBs, {n} cross-DB chains, all verified")


test_cross_db_transfer()
INFO("\nAll tests passed!")
