"""E2E stress test: multiple workers writing to the same DB concurrently.

Verifies no data corruption or lost writes when 2 workers concurrently
write 25 objects each (50 total) to the same database.
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_stress_concurrent_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config


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

    assert len(master.completed_tasks) >= 50, \
        f"Expected >= 50 completed, got {len(master.completed_tasks)}"
    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    for i in range(50):
        val = db.read_object(f"concurrent_{i}")
        assert val == i, f"concurrent_{i} should be {i}, got {val}"

    print(f"[PASS] test_concurrent_writes: 50 objects written by 2 workers, all readable",
          file=sys.stderr)


test_concurrent_writes()
print("\nAll tests passed!")
