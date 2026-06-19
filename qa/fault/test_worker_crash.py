"""E2E test: worker crash and task recovery on remaining workers."""
from _fly_log import INFO
import time
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_worker_crash_db_{os.getpid()}"


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

    master.launch_local_workers([{}, {}])

    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    for i in range(10):
        write_data(db, f"key_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "At least 1 task should complete before crash"

    worker_pids = master.get_worker_pids()
    assert len(worker_pids) >= 2, f"Need 2+ workers, got {len(worker_pids)}"

    import os, signal
    os.kill(worker_pids[0], signal.SIGKILL)
    os.waitpid(worker_pids[0], 0)
    INFO(f"  Killed worker 0, {len(master.completed_tasks)} tasks completed so far")

    # Remaining worker should pick up all tasks (recovered from dead worker)
    total_done = lambda: len(master.completed_tasks) + len(master.failed_tasks)
    assert wait_for(lambda: total_done() >= 10, timeout=60.0), \
        f"Expected all 10 tasks done, got {total_done()} (completed={len(master.completed_tasks)}, failed={len(master.failed_tasks)})"

    assert len(master.completed_tasks) >= 10, \
        f"Expected all 10 completed (zero task loss), got {len(master.completed_tasks)}"

    INFO(f"[PASS] test_worker_crash: {len(master.completed_tasks)} completed after crash")


test_worker_crash()
