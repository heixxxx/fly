"""Run 1 of moved-DB load_db test.
Creates DB at path A, writes data via tasks, does NOT freeze.
Coordinator moves the DB directory to path B before Run 2.
"""
from _fly_log import INFO
import os
import time



from test import write_data, record
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


import shutil
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 0)


master = get_agent()

master.launch_local_workers([{}])

assert master.wait_workers_registered(timeout=60)
# 原断言：assert master.worker_count >= 1, "Worker should connect"
record("worker_connected", master.worker_count >= 1, detail=master.worker_count)

db = open_db(DB_PATH)
db_path = db.get_db_path()

# Submit tasks that write via worker
write_data(db, "moved/alpha", 42)
write_data(db, "moved/beta", 58)

completed = master.wait_for_all_tasks(expected=2, timeout=30)
# 保留：等待门槛——wait_for_all_tasks 超时返回部分完成列表，等不到 2 个完成时
# 后续校验必然失败；等待语义留在脚本里，不转记录。
assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"  # 保留：等待门槛

# Master writes
db.write_object("moved/master_key", "master_value")
db.write_object("moved/dict", {"x": 1})

time.sleep(0.5)

# Verify before exit
# 原断言：assert db.read_object("moved/master_key") == "master_value"
record("read_master_key", db.read_object("moved/master_key"))
# 原断言：assert db.read_object("moved/dict") == {"x": 1}
record("read_dict", db.read_object("moved/dict"))

# Write marker
with open(os.path.join(DB_PATH, "_test_db_path"), "w") as f:
    f.write(db_path)

# 原断言：assert os.path.isfile(os.path.join(DB_PATH, "_DB_META")), "_DB_META should exist"
record("db_meta_exists", os.path.isfile(os.path.join(DB_PATH, "_DB_META")), detail=DB_PATH)

INFO(f"[RUN1_MOVED] Created DB at {DB_PATH}: db_path={db_path}")
INFO("[RUN1_MOVED] Wrote 4 objects (2 worker, 2 master)")
