"""Run 1: write+backup data on two virtual hosts.

Two workers with different --host values write data with backup enabled.
Data is backed up across hosts, creating cross-host idx entries.
"""
from _fly_log import INFO
import os
import sys
import time
import shutil

DB_PATH = "/tmp/fly_e2e_backup_load_db_multi_worker"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data, write_data_backup
from fly import open_db, get_config
from fly.runtime import get_agent

# Cleanup
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)
os.makedirs(DB_PATH, exist_ok=True)

get_config().set_int("fail_unscheduleable_tasks", 0)
get_config().set_int("backup_threshold", 1)  # backup every object

master = get_agent()

# Launch two workers on different virtual hosts
# Worker 0: same host as master (host-alpha)
# Worker 1: host-beta (different virtual host)
master.launch_local_workers([{"host": "host-alpha"}, {"host": "host-beta"}])

assert master.wait_for_workers(2), \
    f"Both workers should connect, got {master.worker_count}"

db = open_db(DB_PATH)
db_id = db.get_db_id()

# Write data with backup — triggers cross-host backup
write_data_backup(db, "shared/alpha_data", 42)
write_data_backup(db, "shared/beta_data", 99)

# Write non-backup data — only exists in host-alpha's idx, no backup copy
write_data(db, "local/only_alpha", "alpha_value")
write_data(db, "local/alpha_secret", 777)
write_data(db, "local/alpha_blob", "x" * 1000)

# Write non-backup data on host-beta — only exists in host-beta's idx
write_data(db, "local/beta_only", "beta_value")

# Wait for user tasks
completed = master.wait_for_all_tasks(expected=6, timeout=30)
assert len(completed) >= 6, f"Expected 6 completed tasks, got {len(completed)}"

# Wait for backup tasks to complete (internal __backup_object tasks)
# Use wait_for_all_tasks with expected=None to wait for ALL tasks including backups
master.wait_for_all_tasks(expected=None, timeout=10)

# Save db_id for Run 2
with open(os.path.join(DB_PATH, "_test_db_id"), "w") as f:
    f.write(db_id)

# Verify data is readable
assert db.read_object("shared/alpha_data") == 42
assert db.read_object("shared/beta_data") == 99

INFO(f"[RUN1] db_id={db_id}, wrote 3 objects with backup")

master.stop()
