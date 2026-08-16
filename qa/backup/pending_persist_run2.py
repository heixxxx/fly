"""Helper: Run 2 for pending_task_persist test.

Start new master, call restart_failed_tasks(), launch worker,
verify tasks are re-submitted and gpu task completes once gpu worker available.
"""
from _fly_log import INFO
import time
import os
import shutil

LOG_DIR = os.environ.get("FLY_RUN1_LOG_DIR") or "/tmp/fly_e2e_pending_persist_logs"


from fly import get_config


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def run2():
    get_config().set_int("fail_unscheduleable_tasks", 1)

    failed_file = os.path.join(LOG_DIR, "failed_tasks.bin")

    from fly.runtime import get_agent
    master = get_agent()

    # Launch worker with gpu attribute so gpu task can complete on restart
    master.launch_local_workers([{"attributes": ["gpu"]}])
    assert master.wait_for_workers(1), \
        "Worker should connect"

    # Restart failed tasks from the persisted file
    assert os.path.isfile(failed_file), \
        f"failed_tasks.bin should exist at {failed_file}"

    master.restart_failed_tasks(failed_file)
    INFO("  Run2: restart_failed_tasks called")

    # Wait for gpu task to complete (gpu worker now available)
    # The unresolvable dep task will fail again, but gpu task should succeed
    assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
        f"Expected at least 1 completed after restart, got {len(master.completed_tasks)}"

    completed = len(master.completed_tasks)
    failed = len(master.failed_tasks)
    INFO(f"  Run2: {completed} completed, {failed} failed after restart")

    # The gpu task should have completed
    assert completed >= 1, \
        f"Expected >= 1 task to complete after restart, got {completed}"

    INFO("  Run2: master stopped")


run2()
