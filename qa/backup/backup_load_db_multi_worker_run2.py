"""Run 2: load_db with workers on different virtual hosts, verify distributed reads.

Loads the DB created in Run 1, assigns idx files per hostname to workers,
and verifies that all data (including cross-host backup data) is readable.
"""
from _fly_log import INFO
import os
import time



from test import write_data, compute_sum, record
from fly import load_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly.runtime import get_agent

get_config().set_int("fail_unscheduleable_tasks", 0)

# Read original db_path from Run 1
marker_path = os.path.join(DB_PATH, "_test_db_path")
# 编排脚本挪入的检查（原先在两个 sub case 之间内联断言，重录模式会真的执行
# 内联语句而失败，见 qa/README.md「编排脚本的写法约定」）：时机不变——run1
# 的工作刚结束、本 sub case 开始工作之前。
# 保留：守卫——后续 open 该 marker 文件还要用
assert os.path.isfile(marker_path), "marker should exist after run1"
with open(marker_path) as f:
    original_db_path = f.read().strip()

master = get_agent()

# Launch one worker on host-beta (load_db will assign idx files per hostname)
master.launch_local_workers([{"host": "host-beta"}])

# 保留：等待原语
assert master.wait_for_workers(1), \
    f"host-beta worker should connect, got {master.worker_count}"

# Load DB — should assign idx files per hostname to connected workers
db = load_db(DB_PATH)

# Verify db_path preserved
db_path_ok = db.get_db_path() == original_db_path
# 原断言：assert db.get_db_path() == original_db_path, \
record("load_db/path_preserved", db_path_ok,
       detail={"actual": db.get_db_path(), "expected": original_db_path})

# idx loading is synchronous (IdxLoadCommand -> IdxLoadAck is fast)
# No need to wait

# ── Verify BACKUP data is readable via host-beta's backup copy ──
# These were written with write_data_backup, so copies exist on host-beta's idx.
# They should be readable even if host-alpha has no worker.
alpha = db.read_object("shared/alpha_data")
# 原断言：assert alpha == 42, f"shared/alpha_data should be 42, got {alpha}"
record("readback/alpha_data", alpha, detail=alpha)
beta = db.read_object("shared/beta_data")
# 原断言：assert beta == 99, f"shared/beta_data should be 99, got {beta}"
record("readback/beta_data", beta, detail=beta)

# ── Verify NON-BACKUP data from host-alpha is also readable ──
# load_db should have spawned a host-alpha worker and loaded its idx.
local_val = db.read_object("local/only_alpha")
# 原断言：assert local_val == "alpha_value", f"local/only_alpha should be 'alpha_value', got {local_val}"
record("readback/local_only_alpha", local_val, detail=local_val)

secret = db.read_object("local/alpha_secret")
# 原断言：assert secret == 777, f"local/alpha_secret should be 777, got {secret}"
record("readback/local_alpha_secret", secret, detail=secret)

blob = db.read_object("local/alpha_blob")
# 原断言：assert blob == "x" * 1000, "local/alpha_blob length mismatch"
record("readback/local_alpha_blob", blob, detail=None if not isinstance(blob, str) else len(blob))

# ── Verify NON-BACKUP data from host-beta is readable ──
beta_only = db.read_object("local/beta_only")
# 原断言：assert beta_only == "beta_value", f"local/beta_only should be 'beta_value', got {beta_only}"
record("readback/local_beta_only", beta_only, detail=beta_only)

INFO("[RUN2] All Run 1 data read back successfully")

# Submit new tasks that depend on loaded data
compute_sum(db, "shared/alpha_data", "shared/beta_data", "result/sum")
write_data(db, "result/new_output", "from_run2")

completed = master.wait_for_all_tasks(expected=2, timeout=30)
two_completed = len(completed) >= 2
# 原断言：assert len(completed) >= 2, f"Expected 2 tasks, got {len(completed)}"
record("new_tasks/two_completed", two_completed, detail=len(completed))
# 保留：守卫——后面 read_object 读的是这些任务写出的对象
assert len(completed) >= 2, f"Expected 2 tasks, got {len(completed)}"

# Wait for any pending writes to complete
master.wait_for_all_tasks(expected=None, timeout=5)

# Verify computed results
result_sum = db.read_object("result/sum")
# 原断言：assert result_sum == 141, f"result/sum should be 141 (42+99), got {result_sum}"
record("new_tasks/result_sum", result_sum, detail=result_sum)
new_output = db.read_object("result/new_output")
# 原断言：assert db.read_object("result/new_output") == "from_run2"
record("new_tasks/new_output", new_output, detail=new_output)

INFO(f"[RUN2] New tasks completed: sum={result_sum}")

master.stop()
INFO("[DONE] backup_load_db_multi_worker")
