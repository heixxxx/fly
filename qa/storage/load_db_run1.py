"""Run 1 of two-process load_db test.
Creates DB, writes data via tasks, does NOT freeze.
"""
from _fly_log import INFO
import os
import time

DB_PATH = "/tmp/fly_e2e_load_db_twoproc"


from e2e_tasks import write_data
from fly import open_db
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
for _ in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1, "Worker should connect"

db = open_db(DB_PATH)
db_id = db.get_db_id()

# Submit tasks that write via worker
write_data(db, "stage1/alpha", 100)
write_data(db, "stage1/beta", 200)
write_data(db, "stage1/gamma", "hello")

# Wait for completion
completed = master.wait_for_all_tasks(expected=3, timeout=30)
assert len(completed) >= 3, f"Expected 3 completed tasks, got {len(completed)}"

# Master also writes directly
db.write_object("stage1/master_only", "from_master")
db.write_object("stage1/config", {"version": 1, "source": "run1"})

# Wait for write-back
time.sleep(0.5)

# Verify data readable before exit
assert db.read_object("stage1/master_only") == "from_master"
assert db.read_object("stage1/config") == {"version": 1, "source": "run1"}

# Do NOT freeze — that's Run 2's job

# Print db_id for Run 2 to verify (write to a marker file)
with open(os.path.join(DB_PATH, "_test_db_id"), "w") as f:
    f.write(db_id)

# Verify _DB_META exists
assert os.path.isfile(os.path.join(DB_PATH, "_DB_META")), "_DB_META should exist"

INFO(f"[RUN1] Created DB: db_id={db_id}, path={DB_PATH}")
INFO(f"[RUN1] Wrote 5 objects (3 via worker, 2 via master)")
INFO(f"[RUN1] NOT freezing — exiting for Run 2 to load_db")
