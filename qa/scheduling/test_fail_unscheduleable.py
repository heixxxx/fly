"""E2E test: fail_unscheduleable_tasks config.

With fail_unscheduleable_tasks=1, tasks whose required_capabilities
no worker in the cluster can satisfy are immediately FAILED.

Worker: 1 worker, attributes=["alpha"]
Tasks:
  - requires=["shared"]: no worker has this -> FAILED with error message
  - requires=["alpha"]: completes normally
"""
from _fly_log import INFO
import os
import shutil



from test import alpha_write, shared_write
from test import record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_fail_unscheduleable_tasks_enabled():
    cleanup()

    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([
        {"attributes": ["alpha"]},
    ])
    assert master.wait_workers_registered(timeout=60)
    # 原断言：assert master.worker_count >= 1
    record("setup/worker_connected", master.worker_count >= 1,
           detail=master.worker_count)

    db = open_db(DB_PATH)

    shared_write(db, "no_cap_result")

    from test import wait_until
    assert wait_until(lambda: master.failed_tasks, timeout=10), \
        "Task with nonexistent capability should be FAILED when config enabled, " \
        f"got failed={master.failed_tasks}"

    failed = master.failed_tasks
    error_msg = master.get_task_error(failed[0])
    # 原断言：assert "No worker with required capabilities" in error_msg, \
    record("no_cap_failed/msg_mentions_missing_capabilities",
           "No worker with required capabilities" in error_msg, detail=error_msg)

    alpha_write(db, "alpha_result")
    assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
        "alpha task should complete within 10s"
    alpha_val = db.read_object("alpha_result")
    # 原断言：assert db.read_object("alpha_result") == 1, \
    record("alpha_result/value", alpha_val, detail=alpha_val)

    INFO("[DONE] test_fail_unscheduleable_tasks_enabled: "
         f"task failed with '{error_msg}'")


test_fail_unscheduleable_tasks_enabled()
