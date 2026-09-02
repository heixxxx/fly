"""E2E test: Worker write rejected after freeze broadcast.

Verifies:
  - Worker A freezes DB during task execution
  - Worker B attempts write → master registration rejects (FROZEN_DB)
  - Task FAILS with error_type（脏数据假成功曾是覆盖率测试暴露的生产缺陷：
    end_task 先 clear last_error_type_ 导致 write-rejection 恒不可见——
    2026-09-02 修复为快照语义，写被拒必须以 TaskFailed 暴露）
  - Nothing is written
"""
from _fly_log import INFO
import time
import os
import shutil

from test import freeze_db, write_after_freeze
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")

def cleanup():
    if os.path.isdir(DB_PATH): shutil.rmtree(DB_PATH, ignore_errors=True)

def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition(): return True
        time.sleep(interval)
    return False

def test_freeze_rejects_worker_write():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)
    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([{}, {}])
    assert master.wait_for_workers(2)
    db = open_db(DB_PATH)
    freeze_db(db, [])
    assert wait_for(lambda: len(master.completed_tasks) >= 1)
    write_after_freeze(db, "after_freeze_key", "value")
    # 新语义：写被 master 注册拒绝（FROZEN_DB）→ task 必须失败，绝不静默
    # 假成功；仅 freeze task 一个完成。
    assert wait_for(lambda: len(master.failed_tasks) >= 1), \
        "post-freeze write must FAIL (rejected write is no longer silently dropped)"
    assert len(master.completed_tasks) == 1, \
        f"only the freeze task completes, got {len(master.completed_tasks)}"
    try:
        db.read_object("after_freeze_key")
        raise AssertionError("rejected write must persist nothing")
    except KeyError:
        pass
    INFO("[PASS] test_freeze_rejects_worker_write")

test_freeze_rejects_worker_write()
INFO("\nAll tests passed!")
