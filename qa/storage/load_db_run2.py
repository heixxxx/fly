"""Run 2 of two-process load_db test.
Loads DB from Run 1, reads back data, executes new tasks, freezes.
"""
from _fly_log import INFO
import os
import time



from test import write_data, compute_sum
from fly import load_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


get_config().set_int("fail_unscheduleable_tasks", 0)


# Read original db_path from marker file
marker_path = os.path.join(DB_PATH, "_test_db_path")
assert os.path.isfile(marker_path), f"Marker file not found: {marker_path}"
with open(marker_path) as f:
    original_db_path = f.read().strip()

master = get_agent()

# Do NOT call launch_local_workers here — load_db will spawn
# process workers to restore the old worker idx files.

# ── Load DB from Run 1 ──
db = load_db(DB_PATH)

# Verify db_path matches
assert db.get_db_path() == original_db_path, \
    f"db_path mismatch: {db.get_db_path()} != {original_db_path}"

# ── Read back Run 1 master-written data (available immediately) ──
assert db.read_object("stage1/master_only") == "from_master", \
    "Failed to read master-only data from Run 1"
assert db.read_object("stage1/config") == {"version": 1, "source": "run1"}, \
    "Failed to read config from Run 1"

# ── Wait for worker to process IdxLoadCommand ──
# After load_db(), idx load commands have been SENT but may not be
# PROCESSED yet by the worker. Sleep to allow async processing.
time.sleep(2.0)

# ── Read back Run 1 worker-written data ──
alpha = db.read_object("stage1/alpha")
assert alpha == 100, f"stage1/alpha should be 100, got {alpha}"

beta = db.read_object("stage1/beta")
assert beta == 200, f"stage1/beta should be 200, got {beta}"

gamma = db.read_object("stage1/gamma")
assert gamma == "hello", f"stage1/gamma should be 'hello', got {gamma}"

INFO(f"[RUN2] Successfully read back all Run 1 data")

# ── Submit new tasks that read Run 1 data and produce results ──
compute_sum(db, "stage1/alpha", "stage1/beta", "stage2/sum")
write_data(db, "stage2/final", "completed_by_run2")

# Wait for tasks
completed = master.wait_for_all_tasks(expected=2, timeout=30)
assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"

# Drain write-back for task outputs
time.sleep(0.5)

# Verify task results
stage2_sum = db.read_object("stage2/sum")
assert stage2_sum == 300, f"stage2/sum should be 300, got {stage2_sum}"

assert db.read_object("stage2/final") == "completed_by_run2"

# ── Freeze ──
db.freeze()
assert db.is_frozen(), "DB should be frozen"

# Verify _FROZEN marker
assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "_FROZEN should exist"

# Verify load_meta（db_path 字段已删，只验证 created_at）
meta = db.load_meta()
assert meta.created_at > 0

INFO(f"[RUN2] All data verified, DB frozen successfully")
INFO(f"[RUN2] stage2/sum = {stage2_sum} (100 + 200 from Run 1)")
