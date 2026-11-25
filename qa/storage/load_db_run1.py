"""Run 1 of two-process load_db test.
Creates DB, writes data via tasks, does NOT freeze.
"""
from _fly_log import INFO
import os
import time



from test import write_data, record
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


# Cleanup previous run
import shutil
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 0)


master = get_agent()

# Launch 1 process worker for task execution
master.launch_local_workers([{}])

# Wait for worker
# 保留：等待原语
assert master.wait_workers_registered(timeout=60)
worker_connected = master.worker_count >= 1
# 原断言：assert master.worker_count >= 1, "Worker should connect"
record("worker_connected", worker_connected, detail=master.worker_count)

db = open_db(DB_PATH)
db_path = db.get_db_path()

# Submit tasks that write via worker
write_data(db, "stage1/alpha", 100)
write_data(db, "stage1/beta", 200)
write_data(db, "stage1/gamma", "hello")

# Wait for completion
completed = master.wait_for_all_tasks(expected=3, timeout=30)
# 原断言：assert len(completed) >= 3, f"Expected 3 completed tasks, got {len(completed)}"
record("worker_tasks_completed", len(completed) >= 3, detail=len(completed))
# 非守卫：后面读的是主进程自己写入的对象（stage1/master_only、stage1/config），不依赖这三个任务完成；
# 任务写出的对象由下一个子阶段（run2）读取

# Master also writes directly
db.write_object("stage1/master_only", "from_master")
db.write_object("stage1/config", {"version": 1, "source": "run1"})

# Wait for write-back
time.sleep(0.5)

# Verify data readable before exit
master_only_value = db.read_object("stage1/master_only")
# 原断言：assert db.read_object("stage1/master_only") == "from_master"
record("master_only_value", master_only_value)
config_value = db.read_object("stage1/config")
# 原断言：assert db.read_object("stage1/config") == {"version": 1, "source": "run1"}
record("config_value", config_value)

# Do NOT freeze — that's Run 2's job

# Print db_path for Run 2 to verify (write to a marker file)
with open(os.path.join(DB_PATH, "_test_db_path"), "w") as f:
    f.write(db_path)

# Verify _DB_META exists
db_meta_exists = os.path.isfile(os.path.join(DB_PATH, "_DB_META"))
# 原断言：assert os.path.isfile(os.path.join(DB_PATH, "_DB_META")), "_DB_META should exist"
record("db_meta_exists", db_meta_exists, detail=db_meta_exists)

INFO(f"[RUN1] Created DB: db_path={db_path}, path={DB_PATH}")
INFO("[RUN1] Wrote 5 objects (3 via worker, 2 via master)")
INFO("[RUN1] NOT freezing — exiting for Run 2 to load_db")
