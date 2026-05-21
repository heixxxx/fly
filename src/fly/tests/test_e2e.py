"""E2E test: Master spawns real subprocess workers via fly binary.

Tests the full Phase 3 flow:
  1. Master starts, auto-assigns port
  2. launch_local_workers(mode="process") spawns fly --worker subprocesses
  3. Worker subprocesses connect back to Master
  4. Task submitted, assigned to worker, executed in subprocess
  5. TaskComplete returned to Master
"""
import sys
import os
import time
import shutil

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/agent/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/log/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/storage/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '../../../bazel-bin/src/core/export'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..'))

import _fly_log as log
from fly.agent import Master
from fly.runtime import reset
from fly.task import _serialize_args


def test_process_workers_connect():
    if os.path.exists("test_e2e_logs"):
        shutil.rmtree("test_e2e_logs")
    
    log.init_log("test_e2e_logs", 0)

    try:
        master = Master()
        master.launch_local_workers(
            [{"role": "hybrid"}] * 2,
            mode="process",
        )

        port = master.port
        assert port > 0, f"Expected auto-assigned port, got {port}"
        print(f"  Master port: {port}")
        print(f"  Worker procs: {len(master._worker_procs)}")

        for i in range(20):
            count = master._agent.get_connection_count()
            print(f"  [{i}] connections={count}")
            if count >= 2:
                break
            time.sleep(0.5)

        assert master._agent.get_connection_count() >= 2, \
            f"Expected 2+ connections, got {master._agent.get_connection_count()}"
        print("PASS: test_process_workers_connect")

    finally:
        master.stop()
        log.shutdown_log()
        shutil.rmtree("test_e2e_logs", ignore_errors=True)
        reset()


def test_task_through_process_worker():
    if os.path.exists("test_e2e_logs2"):
        shutil.rmtree("test_e2e_logs2")
    
    log.init_log("test_e2e_logs2", 0)

    try:
        master = Master()
        master.launch_local_workers(
            [{"role": "hybrid"}],
            mode="process",
        )

        port = master.port
        assert port > 0

        for i in range(20):
            count = master._agent.get_connection_count()
            if count >= 1:
                break
            time.sleep(0.5)

        assert master._agent.get_connection_count() >= 1

        master.submit("simple_add", "test_e2e_helper",
                      _serialize_args([3, 4]))

        completed = []
        for i in range(30):
            completed = master.completed_tasks
            running = master.running_tasks
            pending = master.pending_tasks
            print(f"  [{i}] pending={pending} running={running} completed={completed}")
            if completed:
                break
            time.sleep(1.0)

        assert len(completed) >= 1, \
            f"Expected task completion, got pending={pending} running={running} completed={completed}"
        print("PASS: test_task_through_process_worker")

    finally:
        master.stop()
        log.shutdown_log()
        shutil.rmtree("test_e2e_logs2", ignore_errors=True)
        reset()


if __name__ == "__main__":
    test_process_workers_connect()
    print()
    test_task_through_process_worker()
    print("\nAll E2E tests passed!")