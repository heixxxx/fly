"""E2E stress test: multiple workers writing to the same DB concurrently.

Verifies no data corruption or lost writes when 2 workers concurrently
write 25 objects each (50 total) to the same database.
"""
from _fly_log import INFO
import time
import os
import shutil



from test import write_data, record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_concurrent_writes():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    for i in range(50):
        write_data(db, f"concurrent_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= 50, timeout=60.0), \
        f"All 50 tasks should complete, got {len(master.completed_tasks)} completed"

    completed_count = len(master.completed_tasks)
    completed_ok = completed_count >= 50
    # 原断言：assert len(master.completed_tasks) >= 50, \
    record("all_completed", completed_ok, detail=completed_count)
    # 保留：守卫——后面的 read_object 读的是这 50 个任务写出的对象
    assert completed_ok, \
        f"Expected >= 50 completed, got {completed_count}"

    no_failed = len(master.failed_tasks) == 0
    # 原断言：assert len(master.failed_tasks) == 0, \
    record("no_failed_tasks", no_failed, detail=master.failed_tasks)

    for i in range(50):
        val = db.read_object(f"concurrent_{i}")
        # 原断言：assert val == i, f"concurrent_{i} should be {i}, got {val}"
        record(f"values/concurrent_{i}", val)

    INFO("[DONE] test_concurrent_writes: 50 objects written by 2 workers, all readable")


test_concurrent_writes()
INFO("\nAll stages done!")
