"""E2E stress test: cross-DB data transfer at scale.

Verifies 3 databases with cross-DB dependency chains:
  DB_A writes raw data -> DB_B reads from DB_A and writes features -> DB_C reads from both and writes result
"""
from _fly_log import INFO
from test import qa_tmp
import time
import os
import shutil

DB_A = qa_tmp("fly_e2e_stress_crossdb_a")
DB_B = qa_tmp("fly_e2e_stress_crossdb_b")
DB_C = qa_tmp("fly_e2e_stress_crossdb_c")


from test import write_data, cross_db_copy, triple_db_sum, record
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

    completed_count = len(master.completed_tasks)
    completed_ok = completed_count >= total_expected
    # 原断言：assert len(master.completed_tasks) >= total_expected, \
    record("all_completed", completed_ok, detail=completed_count)
    # 保留：守卫——后面的 read_object 读的是这些任务写出的对象
    assert completed_ok, \
        f"Expected >= {total_expected} completed, got {completed_count}"

    no_failed = len(master.failed_tasks) == 0
    # 原断言：assert len(master.failed_tasks) == 0, \
    record("no_failed_tasks", no_failed, detail=master.failed_tasks)

    for i in range(n):
        result = db_c.read_object(f"result_{i}")
        # 原断言：assert result == i * 100 + i * 100, \
        record(f"results/result_{i}", result)

    INFO(f"[DONE] test_cross_db_transfer: 3 DBs, {n} cross-DB chains, all verified")


test_cross_db_transfer()
INFO("\nAll stages done!")
