"""E2E test: task requiring nonexistent capability with fail_unscheduleable_tasks=0.

Verifies that with fail_unscheduleable_tasks=0, submitting a task with
required_capabilities that no worker possesses does NOT fail — it stays pending.

Worker: 1 worker, attributes=["alpha"]
Tasks:
  - requires=["shared"]: no worker has this -> stays pending (never completes, never fails)
  - requires=["alpha"]: should complete normally
"""
from _fly_log import INFO
import time
import os
import shutil



from test import alpha_write, shared_write
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def test_no_matching_worker_never_completes():
    cleanup()
    get_config().set_int("fail_unscheduleable_tasks", 0)

    from fly.runtime import get_agent
    master = get_agent()
    master.launch_local_workers([
        {"attributes": ["alpha"]},
    ])
    assert master.wait_workers_registered(timeout=60)
    assert master.worker_count >= 1

    db = open_db(DB_PATH)

    shared_write(db, "no_cap_result")
    # 竞态观察窗（B 类）：task 无匹配 capability 应停留调度器——既不完成也
    # 不失败。sleep(3) 给「误调度/误失败」留暴露窗口，窗口后快照计数断言
    # 不变量（completed==0 且 failed==0）。
    time.sleep(3)

    completed = master.completed_tasks
    failed = master.failed_tasks

    assert len(completed) == 0, \
        f"Task with nonexistent capability should NOT complete, got {len(completed)} completed"
    assert len(failed) == 0, \
        f"Task with nonexistent capability should NOT fail, got {len(failed)} failed"

    alpha_write(db, "alpha_result")
    from test import wait_until
    assert wait_until(lambda: len(master.completed_tasks) >= 1, timeout=10), \
        "alpha task should complete within 10s"
    assert db.read_object("alpha_result") == 1, \
        "alpha task should complete normally"

    total_completed = len(master.completed_tasks)
    assert total_completed == 1, \
        f"Only alpha task should have completed, got {total_completed}"

    INFO("[PASS] test_no_matching_worker_never_completes: "
          "no-matching-cap task stays in scheduler, no error, no completion")


test_no_matching_worker_never_completes()
