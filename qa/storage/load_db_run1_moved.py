"""Run 1 of moved-DB load_db test.
Creates DB at path A, writes data via tasks, does NOT freeze.
Coordinator moves the DB directory to path B before Run 2.
"""
from _fly_log import INFO
import os
import time



from e2e_tasks import write_data
from fly import open_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")
from fly import get_config
from fly.runtime import get_agent


import shutil
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 0)


master = get_agent()

master.launch_local_workers([{}])

for _ in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1, "Worker should connect"

db = open_db(DB_PATH)
db_id = db.get_db_id()

# Submit tasks that write via worker
write_data(db, "moved/alpha", 42)
write_data(db, "moved/beta", 58)

completed = master.wait_for_all_tasks(expected=2, timeout=30)
assert len(completed) >= 2, f"Expected 2 completed tasks, got {len(completed)}"

# Master writes
db.write_object("moved/master_key", "master_value")
db.write_object("moved/dict", {"x": 1})

time.sleep(0.5)

# Verify before exit
assert db.read_object("moved/master_key") == "master_value"
assert db.read_object("moved/dict") == {"x": 1}

# Write marker
with open(os.path.join(DB_PATH, "_test_db_id"), "w") as f:
    f.write(db_id)

assert os.path.isfile(os.path.join(DB_PATH, "_DB_META")), "_DB_META should exist"

INFO(f"[RUN1_MOVED] Created DB at {DB_PATH}: db_id={db_id}")
INFO(f"[RUN1_MOVED] Wrote 4 objects (2 worker, 2 master)")
