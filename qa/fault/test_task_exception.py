"""E2E test: task raising RuntimeError gets marked as FAILED.

Verifies:
  - failing_task raises RuntimeError with the expected message
  - Master records the task as failed
  - get_task_error() returns the error message
  - The key the task would have written is NOT readable (task didn't complete)
"""
from _fly_log import INFO
import time
import os
import shutil



from test import failing_task, record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


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


def test_task_exception():
    cleanup()

    from fly.runtime import get_agent
    master = get_agent()

    master.launch_local_workers([{}])
    # 保留：等待原语
    assert master.wait_workers_registered(timeout=60)
    wc = master.worker_count
    # 原断言：assert master.worker_count >= 1
    record("workers/at_least_1", wc >= 1, detail=wc)

    db = open_db(DB_PATH)

    failing_task(db, "never_written", "intentional_test_error")

    # 保留：等待原语
    assert wait_for(lambda: len(master.failed_tasks) >= 1), \
        "failing_task should be recorded as failed"

    failed = master.failed_tasks
    failed_not_empty = len(failed) >= 1
    # 原断言：assert len(failed) >= 1, f"Expected at least 1 failed task, got {len(failed)}"
    record("failed_tasks/at_least_1", failed_not_empty, detail=len(failed))
    # 保留：守卫——后面的 failed[0] 依赖失败清单非空
    assert len(failed) >= 1, f"Expected at least 1 failed task, got {len(failed)}"

    error_msg = master.get_task_error(failed[0])
    msg_has_marker = "intentional_test_error" in error_msg
    # 原断言：assert "intentional_test_error" in error_msg, \
    record("failed_tasks/error_msg", msg_has_marker, detail=error_msg)

    # The key should NOT be readable because the task raised before writing
    # 旧写法失效：assert False 抛出的 AssertionError 是 Exception 的子类，会被下方
    # 同一个 except Exception 一并吞掉，读操作没按预期失败时用例照样通过。
    raised = False
    try:
        db.read_object("never_written")
    except Exception:
        raised = True
    # 原断言：assert raised, "read_object should have failed for unwritten key"
    record("never_written/unreadable", raised, detail=raised)

    INFO("[DONE] test_task_exception")


test_task_exception()
