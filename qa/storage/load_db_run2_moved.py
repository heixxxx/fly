"""Run 2 of moved-DB load_db test.
Loads DB from a NEW path (DB was moved by coordinator after Run 1).
This tests that load_db uses the current path, not meta.base_path.
"""
from _fly_log import INFO
import os
import time



from e2e_tasks import write_data, compute_sum
from fly import load_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


get_config().set_int("fail_unscheduleable_tasks", 0)


# Read original db_id from marker file
marker_path = os.path.join(DB_PATH, "_test_db_id")
assert os.path.isfile(marker_path), f"Marker file not found: {marker_path}"
with open(marker_path) as f:
    original_db_id = f.read().strip()

master = get_agent()

# Do NOT call launch_local_workers here — load_db will spawn
# process workers to restore the old worker idx files.

# ── Load DB from MOVED path ──
db = load_db(DB_PATH)

assert db.get_db_id() == original_db_id, \
    f"db_id mismatch: {db.get_db_id()} != {original_db_id}"

# ── Read master-written data (immediate) ──
assert db.read_object("moved/master_key") == "master_value", \
    "Failed to read master data from moved DB"
assert db.read_object("moved/dict") == {"x": 1}, \
    "Failed to read dict from moved DB"

# ── Wait for worker idx load commands to process ──
time.sleep(2.0)

# ── Read worker-written data ──
alpha = db.read_object("moved/alpha")
assert alpha == 42, f"moved/alpha should be 42, got {alpha}"

beta = db.read_object("moved/beta")
assert beta == 58, f"moved/beta should be 58, got {beta}"

INFO(f"[RUN2_MOVED] Successfully read all data from moved DB")

# ── New tasks ──
compute_sum(db, "moved/alpha", "moved/beta", "moved/sum")
write_data(db, "moved/final", "done")

completed = master.wait_for_all_tasks(expected=2, timeout=30)
assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"

time.sleep(0.5)

try:
    sum_val = db.read_object("moved/sum")
    assert sum_val == 100, f"moved/sum should be 42+58=100, got {sum_val}"
except Exception as e:
    INFO(f"[RUN2_MOVED] ERROR reading moved/sum: {type(e).__name__}: {e}")
    raise

try:
    final_val = db.read_object("moved/final")
    assert final_val == "done", f"moved/final should be 'done', got {final_val}"
except Exception as e:
    INFO(f"[RUN2_MOVED] ERROR reading moved/final: {type(e).__name__}: {e}")
    raise

# Freeze
INFO(f"[RUN2_MOVED] Freezing DB...")
db.freeze()
INFO(f"[RUN2_MOVED] Frozen. is_frozen={db.is_frozen()}")
assert db.is_frozen(), "DB should be frozen"
assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "_FROZEN should exist"

meta = db.load_meta()
INFO(f"[RUN2_MOVED] meta: db_id={meta.db_id}, created_at={meta.created_at}")
assert meta.db_id == original_db_id, f"meta.db_id={meta.db_id} != {original_db_id}"
assert meta.created_at > 0, f"meta.created_at={meta.created_at}"

INFO(f"[RUN2_MOVED] All verified, DB frozen at new path {DB_PATH}")
