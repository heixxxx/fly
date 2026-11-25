"""E2E: remove_object Phase 2 — dependent task fails after object removed.
Runs as a separate fly binary process.
"""
from _fly_log import INFO
import time
import os
import shutil



from test import write_data, write_and_remove, read_after_remove, record, wait_until
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 1)

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)
# 原断言：assert master.worker_count >= 1
record("worker_connected", master.worker_count >= 1, detail=master.worker_count)

db = open_db(DB_PATH)

write_data(db, "will_remove", 42)
time.sleep(1)

completed = master.wait_for_all_tasks(expected=1, timeout=15)
# 保留：等待门槛——wait_for_all_tasks 超时返回部分完成列表，等不到完成时
# 后续的删除与依赖读取必然失去前提；等待语义留在脚本里，不转记录。
assert len(completed) >= 1  # 保留：等待门槛

write_and_remove(db, "will_remove", 99)
master.wait_for_all_tasks(expected=2, timeout=15)

removed_full = db.get_full_name("will_remove")
read_after_remove(db, "result", [removed_full])

# 原断言：assert len(failed) >= 1, \
# （替换原来的手写等待循环：轮询「失败任务出现」，超时 20 秒与原循环
#  40 x 0.5s 一致；等不到即断言失败）
assert wait_until(lambda: len(master.failed_tasks) >= 1, timeout=20.0), \
    f"Task depending on removed object should fail, got failed={master.failed_tasks}"

INFO("[DONE] test_remove_then_dependent_task_fails: dependent task failed as expected")
