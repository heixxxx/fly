"""E2E test: fail_unscheduleable_tasks config with unresolvable dependencies.

With fail_unscheduleable_tasks=1, tasks whose input dependencies
can never be satisfied (no task produces the required data) are FAILED
once all other work is done.

Worker: 1 worker, no special attributes
Tasks:
  - write_data(db, "real_key", 1)        -> completes, produces "real_key"
  - read_data(db, "result", ["phantom"]) -> depends on "phantom" which nobody produces -> FAILED
"""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_unresolvable_dep_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, read_data
from fly import open_db
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
    for i in range(40):
        if master.worker_count >= 1:
            break
        time.sleep(0.5)
    assert master.worker_count >= 1

    db = open_db(DB_PATH)

    write_data(db, "real_key", 1)

    read_data(db, "result", [db.get_obj_name("phantom")])

    for i in range(20):
        failed = master.failed_tasks
        if failed:
            break
        time.sleep(0.5)

    assert len(failed) >= 1, \
        f"Task with unresolvable dependency should be FAILED, got failed={failed}"

    error_msg = master.get_task_error(failed[0])
    assert "Unresolvable data dependencies" in error_msg, \
        f"Error message should mention unresolvable dependencies, got: {error_msg}"
    assert "phantom" in error_msg, \
        f"Error message should list the missing dependency, got: {error_msg}"

    for i in range(20):
        if len(master.completed_tasks) >= 1:
            break
        time.sleep(0.5)
    assert db.read_object("real_key") == 1, \
        f"write_data task should still complete normally"

    print(f"[PASS] test_unresolvable_dependency: "
          f"task failed with '{error_msg}'", file=sys.stderr)


test_unresolvable_dependency()
