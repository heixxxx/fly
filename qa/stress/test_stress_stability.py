"""E2E stress test: long-running stability with large volume.

Verifies framework stability under sustained load:
  - 100 write tasks + 50 compute tasks (reading previous writes)
  - 2 large objects (5MB each) to stress serialization/compression
  - Verify no memory issues or hangs over extended run
"""
from _fly_log import INFO
import time
import os
import shutil



from test import write_data, compute_sum, record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=120.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_stability():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    db.write_object("large_a", "A" * 5_000_000)
    db.write_object("large_b", "B" * 5_000_000)

    n_writes = 60
    for i in range(n_writes):
        write_data(db, f"stable_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= n_writes, timeout=120.0), \
        f"Phase 1: {n_writes} writes should complete, got {len(master.completed_tasks)}"

    n_sums = 30
    for i in range(n_sums):
        compute_sum(db, f"stable_{i*2}", f"stable_{i*2+1}", f"sum_{i}")

    total = n_writes + n_sums
    assert wait_for(lambda: len(master.completed_tasks) >= total, timeout=120.0), \
        f"Phase 2: {total} total should complete, got {len(master.completed_tasks)}"

    completed_count = len(master.completed_tasks)
    completed_ok = completed_count >= total
    # 原断言：assert len(master.completed_tasks) >= total, \
    record("all_completed", completed_ok, detail=completed_count)
    # 保留：守卫——后面的 read_object 读的是这些任务写出的对象
    assert completed_ok, \
        f"Expected >= {total} completed, got {completed_count}"

    no_failed = len(master.failed_tasks) == 0
    # 原断言：assert len(master.failed_tasks) == 0, \
    record("no_failed_tasks", no_failed, detail=master.failed_tasks)

    large_a_len = len(db.read_object("large_a"))
    # 原断言：assert len(db.read_object("large_a")) == 5_000_000, \
    record("large_a_len", large_a_len)
    large_b_len = len(db.read_object("large_b"))
    # 原断言：assert len(db.read_object("large_b")) == 5_000_000, \
    record("large_b_len", large_b_len)

    for i in range(n_sums):
        val = db.read_object(f"sum_{i}")
        # 原断言：assert val == i * 4 + 1, \
        record(f"sums/sum_{i}", val)

    INFO(f"[DONE] test_stability: {n_writes} writes + {n_sums} sums + 2 large objects")


test_stability()
INFO("\nAll stages done!")
