"""E2E test: Master stop broadcasts shutdown to all workers.

Scenario:
  1. Start master, launch 2 workers
  2. Verify both workers are connected
  3. Submit a few tasks that complete normally
  4. Call master.stop()
  5. Verify master._running is False
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
    if not master._running:
        master.start()

    master.launch_local_workers([{}, {}])
    assert wait_for(lambda: master._agent.get_connection_count() >= 2), \
        f"Both workers should connect, got {master._agent.get_connection_count()}"

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
    worker_pids = [proc.pid for proc in master._worker_procs]
    assert len(worker_pids) == 2, \
        f"Expected 2 worker processes, got {len(worker_pids)}"
    print(f"  Worker PIDs: {worker_pids}", file=sys.stderr)

    # -- Phase 3: Stop master (should broadcast shutdown to workers) --
    master.stop()

    # -- Phase 4: Verify master is stopped --
    assert not master._running, \
        "Phase 4: master._running should be False after stop()"

    # -- Phase 5: Verify worker processes have exited --
    for proc in master._worker_procs:
        # proc.wait() already called in master.stop(), poll should return exitcode
        assert proc.poll() is not None, \
            f"Phase 5: worker process pid={proc.pid} should have exited"

    # Also check the original PIDs are gone (no zombie processes)
    for pid in worker_pids:
        try:
            os.kill(pid, 0)
            # If we get here, the process is still alive - that's a failure
            assert False, \
                f"Phase 5: worker pid={pid} should be terminated after shutdown"
        except ProcessLookupError:
            pass  # Expected - process is gone
        except PermissionError:
            pass  # Also acceptable - process is gone from our view

    print(f"  Phase 5: all worker processes exited gracefully", file=sys.stderr)

    # -- Phase 6: Verify no worker procs left in master --
    assert len(master._worker_procs) == 0, \
        f"Phase 6: worker_procs should be cleared, got {len(master._worker_procs)}"

    del db
    print("[PASS] test_shutdown_broadcast", file=sys.stderr)


if __name__ == "__main__":
    test_shutdown_broadcast()
