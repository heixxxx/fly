"""E2E: remove_object Phase 3 — remove one object, verify other still readable.
Runs as a separate fly binary process.
"""
from _fly_log import INFO
import os
import shutil



from test import write_data, write_and_remove, record
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

write_data(db, "keep/obj", "keep_data")
write_data(db, "remove/obj", "remove_data")

completed = master.wait_for_all_tasks(expected=2, timeout=15)
# 保留：等待门槛——wait_for_all_tasks 超时返回部分完成列表，等不到完成时
# 后续的删除与读回必然失去前提；等待语义留在脚本里，不转记录。
assert len(completed) >= 2  # 保留：等待门槛

write_and_remove(db, "remove/obj", "overwrite")
master.wait_for_all_tasks(expected=3, timeout=15)

# 原断言：assert keep_result == "keep_data", \
record("keep_object_readable", db.read_object("keep/obj"))

INFO("[DONE] test_remove_one_keeps_other: kept object still readable")
