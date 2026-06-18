"""E2E test: restart_failed_tasks full lifecycle.

Phase 1: 1 worker (no attributes), submit 3 tasks
  - write_data(db, "real_key", 1)                       -> succeeds
  - write_data_needs_phantom(db, "dep_result", "done")  -> fails (missing dep)
  - gpu_write(db, "gpu_result", 42)                     -> fails (no gpu worker)

Phase 2: Fix data dep + restart
  - db.write_object("phantom", "data")
  - restart_failed_tasks()
  - dep task completes, gpu task re-fails, file still has 1 record

Phase 3: Launch gpu worker + restart
  - Launch worker with attributes=["gpu"]
  - restart_failed_tasks()
  - gpu task completes, failed_tasks.bin deleted

Phase 4: Verify no persisted failures remain
"""
from _fly_log import INFO
import time
import sys
import os
import shutil

DB_PATH = f"/tmp/fly_e2e_restart_lifecycle_db_{os.getpid()}"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, write_data_needs_phantom, gpu_write
from fly import open_db
from fly import get_config


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


def test_restart_failed_tasks_lifecycle():
    cleanup()

    get_config().set_int("fail_unscheduleable_tasks", 1)

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])
    assert master.wait_for_workers(1), \
        "Worker 1 should connect"

    db = open_db(DB_PATH)
    log_dir = get_config().get_str("log_dir")
    failed_file = os.path.join(log_dir, "failed_tasks.bin")

    # ── Phase 1: Submit tasks, expect partial failure ──

    write_data(db, "real_key", 1)
    write_data_needs_phantom(db, "dep_result", "done")
    gpu_write(db, "gpu_result", 42)

    assert wait_for(lambda: len(master.failed_tasks) >= 2), \
        f"Phase 1: expected 2 failed, got {master.failed_tasks}"
    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "Phase 1: write_data should complete"

    p1_completed = len(master.completed_tasks)
    p1_failed = len(master.failed_tasks)
    assert p1_completed == 1, f"Phase 1: expected 1 completed, got {p1_completed}"
    assert p1_failed == 2, f"Phase 1: expected 2 failed, got {p1_failed}"
    assert os.path.isfile(failed_file), \
        "Phase 1: failed_tasks.bin should exist"
    INFO(f"  Phase 1 OK: {p1_completed} completed, {p1_failed} failed")

    # ── Phase 2: Fix data dependency, restart ──

    db.write_object("phantom", "data")
    master.restart_failed_tasks(failed_file)

    assert wait_for(lambda: len(master.completed_tasks) >= 2), \
        f"Phase 2: dep task should complete, got {len(master.completed_tasks)} completed"
    assert wait_for(lambda: len(master.failed_tasks) >= 1), \
        "Phase 2: gpu task should re-fail"

    p2_completed = len(master.completed_tasks)
    p2_failed_ids = master.failed_tasks
    assert p2_completed == 2, f"Phase 2: expected 2 completed, got {p2_completed}"
    assert len(p2_failed_ids) >= 1, \
        f"Phase 2: expected at least 1 failed (gpu), got {len(p2_failed_ids)}"

    gpu_error = master.get_task_error(p2_failed_ids[-1])
    assert "capabilities" in gpu_error, \
        f"Phase 2: expected capability error, got: {gpu_error}"

    failed_file_2 = os.path.join(log_dir, "failed_tasks.bin")
    assert os.path.isfile(failed_file_2), \
        "Phase 2: failed_tasks.bin should still exist (gpu re-persisted)"
    INFO(f"  Phase 2 OK: {p2_completed} completed, gpu re-failed: {gpu_error}")

    # ── Phase 3: Launch gpu worker, restart ──

    master.launch_local_workers([{"attributes": ["gpu"]}])
    assert master.wait_for_workers(2), \
        "Phase 3: gpu worker should connect"

    master.restart_failed_tasks(failed_file_2)

    assert wait_for(lambda: len(master.completed_tasks) >= 3), \
        f"Phase 3: all tasks should complete, got {len(master.completed_tasks)}"

    p3_completed = len(master.completed_tasks)
    p3_failed = master.failed_tasks
    assert p3_completed == 3, f"Phase 3: expected 3 completed, got {p3_completed}"
    assert len(p3_failed) == 0, f"Phase 3: expected 0 failed, got {p3_failed}"

    assert not os.path.isfile(failed_file_2), \
        "Phase 4: failed_tasks.bin should be deleted"
    INFO(f"  Phase 3+4 OK: {p3_completed} completed, 0 failed, file deleted")

    INFO("[PASS] test_restart_failed_tasks_lifecycle")


test_restart_failed_tasks_lifecycle()
