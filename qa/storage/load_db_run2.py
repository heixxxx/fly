"""Run 2 of two-process load_db test.
Loads DB from Run 1, reads back data, executes new tasks, freezes.
"""
from _fly_log import INFO
import os
import time



from test import write_data, compute_sum, record
from fly import load_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


get_config().set_int("fail_unscheduleable_tasks", 0)

# 编排脚本挪入的检查（原先在两个 sub case 之间内联断言，重录模式会真的执行
# 内联语句而失败，见 qa/README.md「编排脚本的写法约定」）：时机不变——run1
# 的工作刚结束、本 sub case 开始工作之前。
db_dir_exists = os.path.isdir(DB_PATH)
# 原断言：assert os.path.isdir(DB_PATH), "DB should exist after run1"
record("db_dir_exists", db_dir_exists, detail=db_dir_exists)
db_meta_exists = os.path.isfile(os.path.join(DB_PATH, "_DB_META"))
# 原断言：assert os.path.isfile(os.path.join(DB_PATH, "_DB_META")), "_DB_META should exist"
record("db_meta_exists", db_meta_exists, detail=db_meta_exists)
not_frozen_after_run1 = not os.path.isfile(os.path.join(DB_PATH, "_FROZEN"))
# 原断言：assert not os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "should NOT be frozen after run1"
record("not_frozen_after_run1", not_frozen_after_run1, detail=not_frozen_after_run1)


# Read original db_path from marker file
marker_path = os.path.join(DB_PATH, "_test_db_path")
marker_exists = os.path.isfile(marker_path)
# 原断言：assert os.path.isfile(marker_path), f"Marker file not found: {marker_path}"
record("marker_exists", marker_exists, detail=marker_exists)
with open(marker_path) as f:
    original_db_path = f.read().strip()

master = get_agent()

# Do NOT call launch_local_workers here — load_db will spawn
# process workers to restore the old worker idx files.

# ── Load DB from Run 1 ──
db = load_db(DB_PATH)

# Verify db_path matches
db_path_matches = db.get_db_path() == original_db_path
# 原断言：assert db.get_db_path() == original_db_path, \
record("db_path_matches", db_path_matches,
       detail={"actual": db.get_db_path(), "expected": original_db_path})

# ── Read back Run 1 master-written data (available immediately) ──
master_only_value = db.read_object("stage1/master_only")
# 原断言：assert db.read_object("stage1/master_only") == "from_master", \
record("master_only_value", master_only_value)
config_value = db.read_object("stage1/config")
# 原断言：assert db.read_object("stage1/config") == {"version": 1, "source": "run1"}, \
record("config_value", config_value)

# ── Wait for worker to process IdxLoadCommand ──
# After load_db(), idx load commands have been SENT but may not be
# PROCESSED yet by the worker. Sleep to allow async processing.
time.sleep(2.0)

# ── Read back Run 1 worker-written data ──
alpha = db.read_object("stage1/alpha")
# 原断言：assert alpha == 100, f"stage1/alpha should be 100, got {alpha}"
record("alpha_value", alpha)

beta = db.read_object("stage1/beta")
# 原断言：assert beta == 200, f"stage1/beta should be 200, got {beta}"
record("beta_value", beta)

gamma = db.read_object("stage1/gamma")
# 原断言：assert gamma == "hello", f"stage1/gamma should be 'hello', got {gamma}"
record("gamma_value", gamma)

INFO("[RUN2] Successfully read back all Run 1 data")

# ── Submit new tasks that read Run 1 data and produce results ──
compute_sum(db, "stage1/alpha", "stage1/beta", "stage2/sum")
write_data(db, "stage2/final", "completed_by_run2")

# Wait for tasks
completed = master.wait_for_all_tasks(expected=2, timeout=30)
# 原断言：assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"
record("new_tasks_completed", len(completed) >= 2, detail=len(completed))

# Drain write-back for task outputs
time.sleep(0.5)

# Verify task results
stage2_sum = db.read_object("stage2/sum")
# 原断言：assert stage2_sum == 300, f"stage2/sum should be 300, got {stage2_sum}"
record("stage2_sum", stage2_sum)

final_value = db.read_object("stage2/final")
# 原断言：assert db.read_object("stage2/final") == "completed_by_run2"
record("final_value", final_value)

# ── Freeze ──
db.freeze()
db_frozen = db.is_frozen()
# 原断言：assert db.is_frozen(), "DB should be frozen"
record("db_frozen", db_frozen, detail=db_frozen)

# Verify _FROZEN marker
frozen_marker_exists = os.path.isfile(os.path.join(DB_PATH, "_FROZEN"))
# 原断言：assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "_FROZEN should exist"
record("frozen_marker_exists", frozen_marker_exists, detail=frozen_marker_exists)

# Verify load_meta（db_path 字段已删，只验证 created_at）
meta = db.load_meta()
meta_created_at_positive = meta.created_at > 0
# 原断言：assert meta.created_at > 0
record("meta_created_at_positive", meta_created_at_positive, detail=meta.created_at)

INFO("[RUN2] All data verified, DB frozen successfully")
INFO(f"[RUN2] stage2/sum = {stage2_sum} (100 + 200 from Run 1)")
