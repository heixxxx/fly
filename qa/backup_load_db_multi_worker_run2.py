"""Run 2: load_db with workers on different virtual hosts, verify distributed reads.

Loads the DB created in Run 1, assigns idx files per hostname to workers,
and verifies that all data (including cross-host backup data) is readable.
"""
from _fly_log import INFO
import os
import sys
import time

DB_PATH = "/tmp/fly_e2e_backup_load_db_multi_worker"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, compute_sum
from fly import load_db, get_config
from fly.runtime import get_agent

get_config().set_int("fail_unscheduleable_tasks", 0)

# Read original db_id from Run 1
marker_path = os.path.join(DB_PATH, "_test_db_id")
with open(marker_path) as f:
    original_db_id = f.read().strip()

master = get_agent()

# Launch one worker on host-beta (load_db will assign idx files per hostname)
master.launch_local_workers([{"host": "host-beta"}])

assert master.wait_for_workers(1), \
    f"host-beta worker should connect, got {master.worker_count}"

# Load DB — should assign idx files per hostname to connected workers
db = load_db(DB_PATH)

# Verify db_id preserved
assert db.get_db_id() == original_db_id, \
    f"db_id mismatch: {db.get_db_id()} != {original_db_id}"

# idx loading is synchronous (IdxLoadCommand -> IdxLoadAck is fast)
# No need to wait

# ── Verify BACKUP data is readable via host-beta's backup copy ──
# These were written with write_data_backup, so copies exist on host-beta's idx.
# They should be readable even if host-alpha has no worker.
alpha = db.read_object("shared/alpha_data")
assert alpha == 42, f"shared/alpha_data should be 42, got {alpha}"
beta = db.read_object("shared/beta_data")
assert beta == 99, f"shared/beta_data should be 99, got {beta}"

# ── Verify NON-BACKUP data from host-alpha is also readable ──
# load_db should have spawned a host-alpha worker and loaded its idx.
local_val = db.read_object("local/only_alpha")
assert local_val == "alpha_value", f"local/only_alpha should be 'alpha_value', got {local_val}"

secret = db.read_object("local/alpha_secret")
assert secret == 777, f"local/alpha_secret should be 777, got {secret}"

blob = db.read_object("local/alpha_blob")
assert blob == "x" * 1000, f"local/alpha_blob length mismatch"

# ── Verify NON-BACKUP data from host-beta is readable ──
beta_only = db.read_object("local/beta_only")
assert beta_only == "beta_value", f"local/beta_only should be 'beta_value', got {beta_only}"

INFO("[RUN2] All Run 1 data read back successfully")

# Submit new tasks that depend on loaded data
compute_sum(db, "shared/alpha_data", "shared/beta_data", "result/sum")
write_data(db, "result/new_output", "from_run2")

completed = master.wait_for_all_tasks(expected=2, timeout=30)
assert len(completed) >= 2, f"Expected 2 tasks, got {len(completed)}"

# Wait for any pending writes to complete
master.wait_for_all_tasks(expected=None, timeout=5)

# Verify computed results
result_sum = db.read_object("result/sum")
assert result_sum == 141, f"result/sum should be 141 (42+99), got {result_sum}"

assert db.read_object("result/new_output") == "from_run2"

INFO(f"[RUN2] New tasks completed: sum={result_sum}")

master.stop()
INFO("[PASS] backup_load_db_multi_worker")
