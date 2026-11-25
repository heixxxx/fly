"""E2E test: worker crash and task recovery on remaining workers."""
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

    # 保留：等待原语
    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    db = open_db(DB_PATH)

    for i in range(10):
        write_data(db, f"key_{i}", i)

    # 保留：等待原语
    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "At least 1 task should complete before crash"

    worker_pids = master.get_worker_pids()
    two_plus_workers = len(worker_pids) >= 2
    # 原断言：assert len(worker_pids) >= 2, f"Need 2+ workers, got {len(worker_pids)}"
    record("prekill/two_plus_workers", two_plus_workers, detail=worker_pids)
    # 保留：守卫——后面的 worker_pids[0] 依赖至少有两个 worker 进程
    assert len(worker_pids) >= 2, f"Need 2+ workers, got {len(worker_pids)}"

    import os, signal
    os.kill(worker_pids[0], signal.SIGKILL)
    os.waitpid(worker_pids[0], 0)
    INFO(f"  Killed worker 0, {len(master.completed_tasks)} tasks completed so far")

    # Remaining worker should pick up all tasks (recovered from dead worker)
    total_done = lambda: len(master.completed_tasks) + len(master.failed_tasks)
    # 保留：等待原语
    assert wait_for(lambda: total_done() >= 10, timeout=60.0), \
        f"Expected all 10 tasks done, got {total_done()} (completed={len(master.completed_tasks)}, failed={len(master.failed_tasks)})"

    completed_final = len(master.completed_tasks)
    all_ten_completed = completed_final >= 10
    # 原断言：assert len(master.completed_tasks) >= 10, \
    record("recovery/all_ten_completed", all_ten_completed, detail=completed_final)

    INFO(f"[DONE] test_worker_crash: {len(master.completed_tasks)} completed after crash")


test_worker_crash()
