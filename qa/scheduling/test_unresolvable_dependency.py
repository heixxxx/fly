"""E2E test: fail_unscheduleable_tasks config with unresolvable dependencies.

With fail_unscheduleable_tasks=1, tasks whose input dependencies
can never be satisfied (no task produces the required data) are FAILED
once all other work is done.

Worker: 1 worker, no special attributes
Tasks:
  - write_data(db, "real_key", 1)        -> completes, produces "real_key"
  - read_data(db, "result", ["phantom"]) -> depends on "phantom" which nobody produces -> FAILED
"""
from _fly_log import INFO
import os
import shutil



from test import write_data, read_data
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_unresolvable_dependency():
    cleanup()

    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}])
    assert master.wait_workers_registered(timeout=60)
    assert master.worker_count >= 1

    db = open_db(DB_PATH)

    write_data(db, "real_key", 1)

    read_data(db, "result", [db.get_full_name("phantom")])

    from test import wait_until
    assert wait_until(lambda: master.failed_tasks, timeout=10), \
        "Task with unresolvable dependency should be FAILED, " \
        f"got failed={master.failed_tasks}"

    failed = master.failed_tasks
    error_msg = master.get_task_error(failed[0])
    assert "Unresolvable data dependencies" in error_msg, \
        f"Error message should mention unresolvable dependencies, got: {error_msg}"
    assert "phantom" in error_msg, \
        f"Error message should list the missing dependency, got: {error_msg}"

    assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
        "write_data task should complete within 10s"
    assert db.read_object("real_key") == 1, \
        "write_data task should still complete normally"

    INFO("[PASS] test_unresolvable_dependency: "
          f"task failed with '{error_msg}'")


test_unresolvable_dependency()
