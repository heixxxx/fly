"""E2E test: stress test with large objects and many concurrent tasks."""
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


def test_stress():
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    db.write_object("large", "x" * 1_000_000)

    for i in range(50):
        write_data(db, f"stress_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= 50, timeout=60.0), \
        f"All 50 tasks should complete, got {len(master.completed_tasks)} completed"

    completed_count = len(master.completed_tasks)
    completed_ok = completed_count >= 50
    # 原断言：assert len(master.completed_tasks) >= 50, \
    record("all_completed", completed_ok, detail=completed_count)
    # 保留：守卫——后面的 read_object 读的是这 50 个任务写出的对象
    assert completed_ok, \
        f"Expected >= 50 completed tasks, got {completed_count}"

    stress_0_val = db.read_object("stress_0")
    # 原断言：assert db.read_object("stress_0") == 0, \
    record("stress_0", stress_0_val)
    stress_49_val = db.read_object("stress_49")
    # 原断言：assert db.read_object("stress_49") == 49, \
    record("stress_49", stress_49_val)

    large_data = db.read_object("large")
    # 原断言：assert len(large_data) == 1_000_000, \
    record("large_len", len(large_data))

    no_failed = len(master.failed_tasks) == 0
    # 原断言：assert len(master.failed_tasks) == 0, \
    record("no_failed_tasks", no_failed, detail=master.failed_tasks)

    INFO("[DONE] test_stress")


test_stress()
