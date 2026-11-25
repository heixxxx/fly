"""Phase 1: Master writes data → @wait_obj sees it immediately → reads and verifies."""
from _fly_log import INFO
import time
import os
import shutil


from fly import open_db, get_config, wait_obj
from test import record
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
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
record("setup/worker_connected", master.worker_count >= 1,
       detail=master.worker_count)

db = open_db(DB_PATH)

@wait_obj(inputs=lambda d, k: [d.get_full_name(k)])
def wait_master_data(d, k):
    # 这个函数在本脚本里由主进程直接调用，record 可用。
    data_value = d.read_object(k)
    # 原断言：assert d.read_object(k) == "sync_data"
    record("wait_master_data/data", data_value, detail=k)
    return "ok"

db.write_object("master_key", "sync_data")
result = wait_master_data(db, "master_key")
# 原断言：assert result == "ok"
record("wait_master_data/result", result, detail=result)

INFO("[DONE] test_wait_obj_master_write")
