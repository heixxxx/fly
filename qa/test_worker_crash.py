"""E2E test: worker crash and task recovery on remaining workers."""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_worker_crash_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def test_worker_crash():
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

    for i in range(10):
        write_data(db, f"key_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "At least 1 task should complete before crash"

    master._worker_procs[0].kill()
    master._worker_procs[0].wait()
    print(f"  Killed worker 0, {len(master.completed_tasks)} tasks completed so far",
          file=sys.stderr)

    # Remaining worker should pick up all tasks (recovered from dead worker)
    total_done = lambda: len(master.completed_tasks) + len(master.failed_tasks)
    assert wait_for(lambda: total_done() >= 10, timeout=60.0), \
        f"Expected all 10 tasks done, got {total_done()} (completed={len(master.completed_tasks)}, failed={len(master.failed_tasks)})"

    assert len(master.completed_tasks) >= 10, \
        f"Expected all 10 completed (zero task loss), got {len(master.completed_tasks)}"

    del db
    master.stop()
    print(f"[PASS] test_worker_crash: {len(master.completed_tasks)} completed after crash",
          file=sys.stderr)


if __name__ == "__main__":
    test_worker_crash()
