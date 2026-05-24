"""E2E test: stress test with large objects and many concurrent tasks."""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_stress_db"

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


def test_stress():
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}, {}])

    assert wait_for(lambda: master._agent.get_connection_count() >= 2), \
        f"Both workers should connect, got {master._agent.get_connection_count()}"

    db = open_db(DB_PATH)

    db.write_object("large", "x" * 1_000_000)

    for i in range(50):
        write_data(db, f"stress_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= 50, timeout=60.0), \
        f"All 50 tasks should complete, got {len(master.completed_tasks)} completed"

    assert len(master.completed_tasks) >= 50, \
        f"Expected >= 50 completed tasks, got {len(master.completed_tasks)}"

    assert db.read_object("stress_0") == 0, \
        f"stress_0 should be 0, got {db.read_object('stress_0')}"
    assert db.read_object("stress_49") == 49, \
        f"stress_49 should be 49, got {db.read_object('stress_49')}"

    large_data = db.read_object("large")
    assert len(large_data) == 1_000_000, \
        f"large object should be 1_000_000 chars, got {len(large_data)}"

    assert len(master.failed_tasks) == 0, \
        f"Expected 0 failed tasks, got {len(master.failed_tasks)}"

    del db
    master.stop()
    print("[PASS] test_stress", file=sys.stderr)


if __name__ == "__main__":
    test_stress()
