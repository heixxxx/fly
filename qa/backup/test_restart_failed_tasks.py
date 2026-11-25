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
import os
import shutil



from test import write_data, write_data_needs_phantom, gpu_write, record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
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
    # 保留：等待原语
    assert master.wait_for_workers(1), \
        "Worker 1 should connect"

    db = open_db(DB_PATH)
    # 失败记录按归属 db 落盘（Task db 归属规则）：task 带 db 参数 → {db_path}/failed_tasks.bin
    failed_file = os.path.join(DB_PATH, "failed_tasks.bin")

    # ── Phase 1: Submit tasks, expect partial failure ──

    write_data(db, "real_key", 1)
    write_data_needs_phantom(db, "dep_result", "done")
    gpu_write(db, "gpu_result", 42)

    # 保留：等待原语
    assert wait_for(lambda: len(master.failed_tasks) >= 2), \
        f"Phase 1: expected 2 failed, got {master.failed_tasks}"
    # 保留：等待原语
    assert wait_for(lambda: len(master.completed_tasks) >= 1), \
        "Phase 1: write_data should complete"

    p1_completed = len(master.completed_tasks)
    p1_failed = len(master.failed_tasks)
    p1_completed_ok = p1_completed == 1
    # 原断言：assert p1_completed == 1, f"Phase 1: expected 1 completed, got {p1_completed}"
    record("phase1/completed_count", p1_completed_ok, detail=p1_completed)
    p1_failed_ok = p1_failed == 2
    # 原断言：assert p1_failed == 2, f"Phase 1: expected 2 failed, got {p1_failed}"
    record("phase1/failed_count", p1_failed_ok, detail=p1_failed)
    bin_persisted = os.path.isfile(failed_file)
    # 原断言：assert os.path.isfile(failed_file), \
    record("phase1/failed_bin_persisted", bin_persisted, detail=failed_file)
    # 非守卫：failed_tasks.bin 只被 restart_failed_tasks 消费，缺失时它静默跳过，后面的代码不打开这个文件
    INFO(f"  Phase 1 OK: {p1_completed} completed, {p1_failed} failed")

    # ── Phase 2: Fix data dependency, restart ──

    db.write_object("phantom", "data")
    master.restart_failed_tasks(db)

    # 保留：等待原语
    assert wait_for(lambda: len(master.completed_tasks) >= 2), \
        f"Phase 2: dep task should complete, got {len(master.completed_tasks)} completed"
    # 保留：等待原语
    assert wait_for(lambda: len(master.failed_tasks) >= 1), \
        "Phase 2: gpu task should re-fail"

    p2_completed = len(master.completed_tasks)
    p2_failed_ids = master.failed_tasks
    p2_completed_ok = p2_completed == 2
    # 原断言：assert p2_completed == 2, f"Phase 2: expected 2 completed, got {p2_completed}"
    record("phase2/completed_count", p2_completed_ok, detail=p2_completed)
    p2_failed_not_empty = len(p2_failed_ids) >= 1
    # 原断言：assert len(p2_failed_ids) >= 1, \
    record("phase2/failed_at_least_1", p2_failed_not_empty, detail=len(p2_failed_ids))
    # 保留：守卫——后面的 p2_failed_ids[-1] 依赖失败清单非空
    assert len(p2_failed_ids) >= 1, \
        f"Phase 2: expected at least 1 failed (gpu), got {len(p2_failed_ids)}"

    gpu_error = master.get_task_error(p2_failed_ids[-1])
    gpu_error_is_capability = "capabilities" in gpu_error
    # 原断言：assert "capabilities" in gpu_error, \
    record("phase2/gpu_error_is_capability", gpu_error_is_capability, detail=gpu_error)

    failed_file_2 = os.path.join(DB_PATH, "failed_tasks.bin")
    bin_repersisted = os.path.isfile(failed_file_2)
    # 原断言：assert os.path.isfile(failed_file_2), \
    record("phase2/failed_bin_persisted", bin_repersisted, detail=failed_file_2)
    # 非守卫：failed_tasks.bin 只被 restart_failed_tasks 消费，缺失时它静默跳过，后面的代码不打开这个文件
    INFO(f"  Phase 2 OK: {p2_completed} completed, gpu re-failed: {gpu_error}")

    # ── Phase 3: Launch gpu worker, restart ──

    master.launch_local_workers([{"attributes": ["gpu"]}])
    # 保留：等待原语
    assert master.wait_for_workers(2), \
        "Phase 3: gpu worker should connect"

    master.restart_failed_tasks(db)

    # 保留：等待原语
    assert wait_for(lambda: len(master.completed_tasks) >= 3), \
        f"Phase 3: all tasks should complete, got {len(master.completed_tasks)}"

    p3_completed = len(master.completed_tasks)
    p3_failed = master.failed_tasks
    p3_completed_ok = p3_completed == 3
    # 原断言：assert p3_completed == 3, f"Phase 3: expected 3 completed, got {p3_completed}"
    record("phase3/completed_count", p3_completed_ok, detail=p3_completed)
    p3_no_failed = len(p3_failed) == 0
    # 原断言：assert len(p3_failed) == 0, f"Phase 3: expected 0 failed, got {p3_failed}"
    record("phase3/no_failed_tasks", p3_no_failed, detail=p3_failed)

    bin_deleted = not os.path.isfile(failed_file_2)
    # 原断言：assert not os.path.isfile(failed_file_2), \
    record("phase4/failed_bin_deleted", bin_deleted, detail=os.path.isfile(failed_file_2))
    INFO(f"  Phase 3+4 OK: {p3_completed} completed, 0 failed, file deleted")

    INFO("[DONE] test_restart_failed_tasks_lifecycle")


test_restart_failed_tasks_lifecycle()
