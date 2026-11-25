"""Run 2 of moved-DB load_db test.
Loads DB from a NEW path (DB was moved by coordinator after Run 1).
This tests that load_db uses the current path, not meta.db_path.
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


# db_path 废弃：db_path == db_path。搬目录后 db_path 自然变成新 path。
# _test_db_path marker 仅用于跨进程确认 _DB_META 存在（不再比对 db_path）。
marker_path = os.path.join(DB_PATH, "_test_db_path")
marker_exists = os.path.isfile(marker_path)
# 原断言：assert os.path.isfile(marker_path), f"Marker file not found: {marker_path}"
record("marker_exists", marker_exists, detail=marker_exists)

master = get_agent()

# Do NOT call launch_local_workers here — load_db will spawn
# process workers to restore the old worker idx files.

# ── Load DB from MOVED path ──
db = load_db(DB_PATH)

# db_path 废弃：db_path 现在是 db_path 别名，搬目录后 == 当前 DB_PATH
db_path_is_current = db.get_db_path() == DB_PATH
# 原断言：assert db.get_db_path() == DB_PATH, \
record("db_path_is_current", db_path_is_current,
       detail={"actual": db.get_db_path(), "expected": DB_PATH})

# ── Read master-written data (immediate) ──
master_key_value = db.read_object("moved/master_key")
# 原断言：assert db.read_object("moved/master_key") == "master_value", \
record("master_key_value", master_key_value)
dict_value = db.read_object("moved/dict")
# 原断言：assert db.read_object("moved/dict") == {"x": 1}, \
record("dict_value", dict_value)

# ── Wait for worker idx load commands to process ──
time.sleep(2.0)

# ── Read worker-written data ──
alpha = db.read_object("moved/alpha")
# 原断言：assert alpha == 42, f"moved/alpha should be 42, got {alpha}"
record("alpha_value", alpha)

beta = db.read_object("moved/beta")
# 原断言：assert beta == 58, f"moved/beta should be 58, got {beta}"
record("beta_value", beta)

INFO("[RUN2_MOVED] Successfully read all data from moved DB")

# ── New tasks ──
compute_sum(db, "moved/alpha", "moved/beta", "moved/sum")
write_data(db, "moved/final", "done")

completed = master.wait_for_all_tasks(expected=2, timeout=30)
# 原断言：assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"
record("new_tasks_completed", len(completed) >= 2, detail=len(completed))

time.sleep(0.5)

try:
    sum_val = db.read_object("moved/sum")
    # 原断言：assert sum_val == 100, f"moved/sum should be 42+58=100, got {sum_val}"
    record("sum_value", sum_val)
except Exception as e:
    INFO(f"[RUN2_MOVED] ERROR reading moved/sum: {type(e).__name__}: {e}")
    raise

try:
    final_val = db.read_object("moved/final")
    # 原断言：assert final_val == "done", f"moved/final should be 'done', got {final_val}"
    record("final_value", final_val)
except Exception as e:
    INFO(f"[RUN2_MOVED] ERROR reading moved/final: {type(e).__name__}: {e}")
    raise

# Freeze
INFO("[RUN2_MOVED] Freezing DB...")
db.freeze()
INFO(f"[RUN2_MOVED] Frozen. is_frozen={db.is_frozen()}")
db_frozen = db.is_frozen()
# 原断言：assert db.is_frozen(), "DB should be frozen"
record("db_frozen", db_frozen, detail=db_frozen)
frozen_marker_exists = os.path.isfile(os.path.join(DB_PATH, "_FROZEN"))
# 原断言：assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "_FROZEN should exist"
record("frozen_marker_exists", frozen_marker_exists, detail=frozen_marker_exists)

meta = db.load_meta()
INFO(f"[RUN2_MOVED] meta: created_at={meta.created_at}")
# db_path 废弃：搬目录后 _DB_META 里的 db_path（旧 path）与当前 db.get_db_path()（新 path）
# 不同是预期的。只验证 _DB_META 有效（created_at > 0）。
meta_created_at_positive = meta.created_at > 0
# 原断言：assert meta.created_at > 0, f"meta.created_at={meta.created_at}"
record("meta_created_at_positive", meta_created_at_positive, detail=meta.created_at)

INFO(f"[RUN2_MOVED] All verified, DB frozen at new path {DB_PATH}")
