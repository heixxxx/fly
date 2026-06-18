"""E2E stress test: long-running stability with large volume.

Verifies framework stability under sustained load:
  - 100 write tasks + 50 compute tasks (reading previous writes)
  - 2 large objects (5MB each) to stress serialization/compression
  - Verify no memory issues or hangs over extended run
"""
from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_stress_stability_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, compute_sum
from fly import open_db, get_config


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

    n_writes = 100
    for i in range(n_writes):
        write_data(db, f"stable_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= n_writes, timeout=120.0), \
        f"Phase 1: {n_writes} writes should complete, got {len(master.completed_tasks)}"

    n_sums = 50
    for i in range(n_sums):
        compute_sum(db, f"stable_{i*2}", f"stable_{i*2+1}", f"sum_{i}")

    total = n_writes + n_sums
    assert wait_for(lambda: len(master.completed_tasks) >= total, timeout=120.0), \
        f"Phase 2: {total} total should complete, got {len(master.completed_tasks)}"

    assert len(master.completed_tasks) >= total, \
        f"Expected >= {total} completed, got {len(master.completed_tasks)}"
    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    assert len(db.read_object("large_a")) == 5_000_000, \
        "large_a should be 5M chars"
    assert len(db.read_object("large_b")) == 5_000_000, \
        "large_b should be 5M chars"

    for i in range(n_sums):
        val = db.read_object(f"sum_{i}")
        assert val == i * 4 + 1, \
            f"sum_{i} should be {i * 4 + 1}, got {val}"

    INFO(f"[PASS] test_stability: {n_writes} writes + {n_sums} sums + 2 large objects")


test_stability()
INFO("\nAll tests passed!")
