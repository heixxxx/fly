"""E2E test: Master stop broadcasts shutdown to all workers.

Scenario:
  1. Start master, launch 2 workers
  2. Verify both workers are connected
  3. Submit a few tasks that complete normally
  4. Call master.stop()
  5. Verify master is not running
  6. Verify worker processes have exited (they received ShutdownMessage)
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_shutdown_broadcast_db"

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


def test_shutdown_broadcast():
    cleanup()

    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}, {}])
    assert master.wait_for_workers(2), \
        f"Both workers should connect, got {master.worker_count}"

    print(f"  Phase 1: 2 workers connected", file=sys.stderr)

    db = open_db(DB_PATH)

    # Submit some tasks to ensure the system is active
    for i in range(5):
        write_data(db, f"key_{i}", i)

    assert wait_for(lambda: len(master.completed_tasks) >= 5), \
        f"Phase 2: 5 tasks should complete, got {len(master.completed_tasks)}"

    print(f"  Phase 2: {len(master.completed_tasks)} tasks completed",
          file=sys.stderr)

    # Capture worker PIDs before stop
    worker_pids = master.get_worker_pids()
    assert len(worker_pids) == 2, \
        f"Expected 2 worker processes, got {len(worker_pids)}"
    print(f"  Worker PIDs: {worker_pids}", file=sys.stderr)

    # -- Phase 3: Stop master (should broadcast shutdown to workers) --
    master.stop()

    # -- Phase 4: Verify master is stopped --
    assert not master.is_running(), \
        "Phase 4: master should not be running after stop()"

    # -- Phase 5: Verify worker processes have exited --
    for pid in worker_pids:
        try:
            os.kill(pid, 0)
            assert False, \
                f"Phase 5: worker pid={pid} should be terminated after shutdown"
        except ProcessLookupError:
            pass
        except PermissionError:
            pass

    print(f"  Phase 5: all worker processes exited gracefully", file=sys.stderr)

    # -- Phase 6: Verify no worker procs left --
    assert len(master.get_worker_pids()) == 0, \
        f"Phase 6: no workers should be running"

    print("[PASS] test_shutdown_broadcast", file=sys.stderr)


test_shutdown_broadcast()
