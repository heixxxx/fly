"""E2E stress test: multiple databases operated in parallel.

Verifies 4 databases, each receiving 10 write tasks simultaneously
across 2 workers. All 40 tasks should complete without interference.
"""
from _fly_log import INFO
from test import qa_tmp
import time
import os
import shutil

DB_PATHS = [qa_tmp(f"fly_e2e_stress_multidb_{i}") for i in range(4)]


from test import write_data, record
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

    completed_count = len(master.completed_tasks)
    completed_ok = completed_count >= total
    # 原断言：assert len(master.completed_tasks) >= total, \
    record("all_completed", completed_ok, detail=completed_count)
    # 保留：守卫——后面的 read_object 读的是这 40 个任务写出的对象
    assert completed_ok, \
        f"Expected >= {total} completed, got {completed_count}"

    no_failed = len(master.failed_tasks) == 0
    # 原断言：assert len(master.failed_tasks) == 0, \
    record("no_failed_tasks", no_failed, detail=master.failed_tasks)

    # 循环外层遍历的是运行时集合（dbs），记录不能写在循环里：先累计成字典，
    # 循环结束后整体记录一次。
    readback_values = {}
    for db_idx, db in enumerate(dbs):
        for i in range(writes_per_db):
            key = f"db{db_idx}_key_{i}"
            val = db.read_object(key)
            readback_values[key] = val
    # 原断言：assert val == db_idx * 100 + i, \
    record("readback_values", readback_values)

    INFO(f"[DONE] test_multi_db_parallel: 4 DBs x {writes_per_db} writes, all verified")


test_multi_db_parallel()
INFO("\nAll stages done!")
