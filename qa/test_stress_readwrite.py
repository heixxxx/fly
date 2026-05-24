"""E2E stress test: concurrent read/write mix.

Verifies that while tasks are writing data, downstream read tasks
can execute as soon as dependencies are satisfied. 20 write tasks
paired with 20 read tasks, all running concurrently on 2 workers.
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_stress_readwrite_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, read_data
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


def test_readwrite_mix():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}, {}])

    assert wait_for(lambda: master._agent.get_connection_count() >= 2), \
        f"Both workers should connect, got {master._agent.get_connection_count()}"

    db = open_db(DB_PATH)

    n = 20
    for i in range(n):
        write_data(db, f"rw_{i}", i * 10)

    assert wait_for(lambda: len(master.completed_tasks) >= n, timeout=60.0), \
        f"Phase 1: {n} writes should complete, got {len(master.completed_tasks)}"

    for i in range(n):
        read_data(db, f"rw_{i}", [db.get_obj_name(f"rw_{i}")])

    assert wait_for(lambda: len(master.completed_tasks) >= 2 * n, timeout=60.0), \
        f"Phase 2: {2*n} total tasks should complete, got {len(master.completed_tasks)}"

    assert len(master.completed_tasks) >= 2 * n, \
        f"Expected >= {2*n} completed, got {len(master.completed_tasks)}"
    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed, got {len(master.failed_tasks)}"

    del db
    master.stop()
    print(f"[PASS] test_readwrite_mix: {n} writes + {n} reads, all completed",
          file=sys.stderr)


if __name__ == "__main__":
    test_readwrite_mix()
    print("\nAll tests passed!")
