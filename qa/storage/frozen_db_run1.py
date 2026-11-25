"""Run 1 of frozen DB load test. Create DB, write data via tasks, freeze."""
from _fly_log import INFO
import os
import time
import shutil



from test import write_data, record
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 0)

master = get_agent()

master.launch_local_workers([{}])

assert master.wait_workers_registered(timeout=60)
# 原断言：assert master.worker_count >= 1, "Worker should connect"
record("worker_connected", master.worker_count >= 1, detail=master.worker_count)

db = open_db(DB_PATH)

write_data(db, "frozen/a", 10)
write_data(db, "frozen/b", 20)
write_data(db, "frozen/c", "hello")

completed = master.wait_for_all_tasks(expected=3, timeout=30)
# 保留：等待门槛——wait_for_all_tasks 超时返回部分完成列表，等不到 3 个完成时
# 后续读回必然失败；等待语义留在脚本里，不转记录。
assert len(completed) >= 3, f"Expected 3 completed tasks, got {len(completed)}"  # 保留：等待门槛

time.sleep(0.5)

# 原断言：assert db.read_object("frozen/a") == 10
record("read_back/frozen_a", db.read_object("frozen/a"))

# 原断言：assert db.read_object("frozen/b") == 20
record("read_back/frozen_b", db.read_object("frozen/b"))

# 原断言：assert db.read_object("frozen/c") == "hello"
record("read_back/frozen_c", db.read_object("frozen/c"))

db.freeze()
# 原断言：assert db.is_frozen(), "DB should be frozen after freeze()"
record("frozen_after_freeze", db.is_frozen(), detail=db.is_frozen())

# 原断言：assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "_FROZEN marker should exist"
record("frozen_marker_exists", os.path.isfile(os.path.join(DB_PATH, "_FROZEN")),
       detail=DB_PATH)

INFO("[RUN1] Created DB, wrote 3 objects, frozen successfully")
