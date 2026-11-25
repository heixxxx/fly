"""E2E test: Master stop broadcasts shutdown to all workers.

Scenario:
  1. Start master, launch 2 workers
  2. Verify both workers are connected
  3. Submit a few tasks that complete normally
  4. Call master.stop()
  5. Verify master is not running
  6. Verify worker processes have exited (they received ShutdownMessage)
"""
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


def test_shutdown_broadcast():
    cleanup()

    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])
    # 保留：等待原语
    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    INFO("  Phase 1: 2 workers connected")

    db = open_db(DB_PATH)

    # Submit some tasks to ensure the system is active
    for i in range(5):
        write_data(db, f"key_{i}", i)

    # 保留：等待原语
    assert wait_for(lambda: len(master.completed_tasks) >= 5), \
        f"Phase 2: 5 tasks should complete, got {len(master.completed_tasks)}"

    INFO(f"  Phase 2: {len(master.completed_tasks)} tasks completed")

    # Capture worker PIDs before stop
    worker_pids = master.get_worker_pids()
    two_worker_pids = len(worker_pids) == 2
    # 原断言：assert len(worker_pids) == 2, \
    record("shutdown/two_worker_pids", two_worker_pids, detail=worker_pids)
    # 保留：守卫——后面 Phase 5 的循环检查依赖列表里确实有这两个进程
    assert len(worker_pids) == 2, \
        f"Expected 2 worker processes, got {len(worker_pids)}"
    INFO(f"  Worker PIDs: {worker_pids}")

    # -- Phase 3: Stop master (should broadcast shutdown to workers) --
    master.stop()

    # -- Phase 4: Verify master is stopped --
    master_stopped = not master.is_running()
    # 原断言：assert not master.is_running(), \
    record("shutdown/master_stopped", master_stopped, detail=master.is_running())

    # -- Phase 5: Verify worker processes have exited --
    for pid in worker_pids:
        try:
            os.kill(pid, 0)
            # 否定性检查里 try 块内原有的 assert False：抛出的 AssertionError 不会被
            # 下面的 except ProcessLookupError/PermissionError 捕获，效果与 raise 相同。
            # 保留：见上
            assert False, \
                f"Phase 5: worker pid={pid} should be terminated after shutdown"
        except ProcessLookupError:
            pass
        except PermissionError:
            pass

    INFO("  Phase 5: all worker processes exited gracefully")

    # -- Phase 6: Verify no worker procs left --
    workers_left = len(master.get_worker_pids())
    no_workers_left = workers_left == 0
    # 原断言：assert len(master.get_worker_pids()) == 0, \
    record("shutdown/no_workers_left", no_workers_left, detail=workers_left)

    INFO("[DONE] test_shutdown_broadcast")


test_shutdown_broadcast()
