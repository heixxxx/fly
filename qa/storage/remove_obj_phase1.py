"""E2E: remove_object Phase 1 — write_and_remove basic.
Runs as a separate fly binary process.
"""
from _fly_log import INFO
import os
import shutil



from test import write_and_remove, record, wait_tasks
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_workers_registered(timeout=60)
# 原断言：assert master.worker_count >= 1
record("worker_connected", master.worker_count >= 1, detail=master.worker_count)

db = open_db(DB_PATH)
write_and_remove(db, "temp/obj", "temp_data")

# 原断言：assert len(completed) >= 1
# （替换原来的手写等待循环：循环内 raise AssertionError 的失败即停语义由
#  wait_tasks 保持——任务一失败就停止等待，错误进记录的 detail；超时 20 秒
#  与原循环 40 x 0.5s 一致）
assert wait_tasks(master, 1, "write_and_remove/no_failed_tasks", timeout=20.0), \
    "write_and_remove task did not complete"

INFO("[DONE] test_remove_object_basic: write+remove completed")
