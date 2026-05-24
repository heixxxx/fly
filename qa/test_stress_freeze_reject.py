"""E2E stress test: freeze DB while many writes are queued.

Verifies that when a DB is frozen, all subsequent write registrations
are rejected. Submits 10 writes, freezes after first batch, then
submits 10 more writes — all should fail.
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_stress_freeze_reject_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, freeze_db, write_after_freeze
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


def test_freeze_reject_stress():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()

    master.launch_local_workers([{}])

    assert wait_for(lambda: master._agent.get_connection_count() >= 1), \
        f"Worker should connect, got {master._agent.get_connection_count()}"

    db = open_db(DB_PATH)

    pre_freeze_count = 10
    for i in range(pre_freeze_count):
        write_data(db, f"pre_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= pre_freeze_count, timeout=60.0), \
        f"Pre-freeze writes should complete, got {len(master.completed_tasks)}"

    freeze_db(db, [])

    assert wait_for(lambda: len(master.completed_tasks) >= pre_freeze_count + 1, timeout=30.0), \
        f"freeze_db task should complete, got {len(master.completed_tasks)}"

    post_freeze_count = 10
    for i in range(post_freeze_count):
        write_after_freeze(db, f"post_{i}", i)

    assert wait_for(lambda: len(master.failed_tasks) >= post_freeze_count, timeout=30.0), \
        f"All post-freeze writes should fail, got {len(master.failed_tasks)} failed, " \
        f"completed={len(master.completed_tasks)}"

    assert len(master.failed_tasks) >= post_freeze_count, \
        f"Expected >= {post_freeze_count} failed, got {len(master.failed_tasks)}"

    for tid in master.failed_tasks:
        error = master.get_task_error(tid)
        assert "frozen" in error.lower(), f"Expected frozen error, got: {error}"

    for i in range(pre_freeze_count):
        val = db.read_object(f"pre_{i}")
        assert val == i, f"pre_{i} should be {i}, got {val}"

    del db
    master.stop()
    print(f"[PASS] test_freeze_reject_stress: {pre_freeze_count} pre-freeze OK, "
          f"{post_freeze_count} post-freeze rejected", file=sys.stderr)


if __name__ == "__main__":
    test_freeze_reject_stress()
    print("\nAll tests passed!")
