"""Run 1: Initial data production with 2 DBs, cross-DB compute, freeze."""
from _fly_log import INFO
import os
import shutil
import time

DB_RAW = "/tmp/fly_complex_db_raw"
DB_FEAT = "/tmp/fly_complex_db_feat"


from e2e_tasks import write_data, cross_db_copy, freeze_db
from fly import open_db
from fly import get_config


def cleanup():
    for p in [DB_RAW, DB_FEAT]:
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


cleanup()

get_config().set_int("fail_unscheduleable_tasks", 1)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
for i in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1, "Worker should connect"

db_raw = open_db(DB_RAW)
db_feat = open_db(DB_FEAT)

write_data(db_raw, "x", 10)
write_data(db_raw, "z", 30)
write_data(db_feat, "y", 20)

assert wait_for(lambda: len(master.completed_tasks) >= 3), \
    f"Phase 1: expected 3 completed, got {len(master.completed_tasks)}"
INFO(f"  Phase 1 OK: {len(master.completed_tasks)} tasks completed")

cross_db_copy(db_feat, db_raw, "x", "feat_xy_from_raw")

assert wait_for(lambda: len(master.completed_tasks) >= 4), \
    f"Phase 2: expected 4 completed, got {len(master.completed_tasks)}"

result = db_feat.read_object("feat_xy_from_raw")
assert result == 10, f"Expected feat_xy_from_raw=10, got {result}"
INFO(f"  Phase 2 OK: cross-DB copy feat_xy_from_raw={result}")

db_raw.write_object("finish", 1)
db_raw.freeze()
INFO("  Phase 3 OK: DB_raw frozen")

INFO("[PASS] Run 1 complete")
