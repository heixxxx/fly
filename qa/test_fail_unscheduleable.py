"""E2E test: fail_unscheduleable_tasks config.

With fail_unscheduleable_tasks=1, tasks whose required_capabilities
no worker in the cluster can satisfy are immediately FAILED.

Worker: 1 worker, attributes=["alpha"]
Tasks:
  - requires=["shared"]: no worker has this -> FAILED with error message
  - requires=["alpha"]: completes normally
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_fail_unsched_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import alpha_write, shared_write
from fly import open_db
from fly import get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_fail_unscheduleable_tasks_enabled():
    cleanup()

    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()
    if not master._running:
        master.start()
    master.launch_local_workers([
        {"attributes": ["alpha"]},
    ], mode="process")
    for i in range(40):
        if master._agent.get_connection_count() >= 1:
            break
        time.sleep(0.5)
    assert master._agent.get_connection_count() >= 1

    db = open_db(DB_PATH)

    shared_write(db, "no_cap_result")

    for i in range(20):
        failed = master.failed_tasks
        if failed:
            break
        time.sleep(0.5)

    assert len(failed) >= 1, \
        f"Task with nonexistent capability should be FAILED when config enabled, got failed={failed}"

    error_msg = master.get_task_error(failed[0])
    assert "No worker with required capabilities" in error_msg, \
        f"Error message should mention missing capabilities, got: {error_msg}"

    alpha_write(db, "alpha_result")
    for i in range(20):
        if len(master.completed_tasks) >= 1:
            break
        time.sleep(0.5)
    assert db.read_object("alpha_result") == 1, \
        f"alpha task should still complete normally"

    del db
    master.stop()
    print(f"[PASS] test_fail_unscheduleable_tasks_enabled: "
          f"task failed with '{error_msg}'", file=sys.stderr)


if __name__ == "__main__":
    test_fail_unscheduleable_tasks_enabled()
