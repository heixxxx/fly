"""Run 2: load_db both DBs (moved), new DB_model, dynamic properties, restart, triple-DB compute."""
from _fly_log import INFO
import os
import shutil
import time

from test import (write_data, cross_db_copy, cross_db_sum,
                       add_alpha_property, alpha_cross_db_copy,
                       gpu_cross_db_copy, triple_db_sum)
from fly import open_db, load_db
from fly import get_config
_DB_DIR = os.environ.get("FLY_DB_DIR") or get_config().get_str("log_dir")
DB_RAW_ORIG = os.path.join(_DB_DIR, "db_raw")
DB_FEAT_ORIG = os.path.join(_DB_DIR, "db_feat")
DB_RAW_MOVED = os.path.join(_DB_DIR, "db_raw_moved")
DB_FEAT_MOVED = os.path.join(_DB_DIR, "db_feat_moved")
DB_MODEL = os.path.join(_DB_DIR, "db_model")
def wait_for(condition, timeout=20.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


shutil.move(DB_RAW_ORIG, DB_RAW_MOVED)
shutil.move(DB_FEAT_ORIG, DB_FEAT_MOVED)
if os.path.isdir(DB_MODEL):
    shutil.rmtree(DB_MODEL, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)

from fly.runtime import get_agent
master = get_agent()

db_raw = load_db(DB_RAW_MOVED)
db_feat = load_db(DB_FEAT_MOVED)
db_model = open_db(DB_MODEL)

x_val = db_raw.read_object("x")
z_val = db_raw.read_object("z")
feat_val = db_feat.read_object("feat_xy_from_raw")
assert x_val == 10, f"Expected x=10, got {x_val}"
assert z_val == 30, f"Expected z=30, got {z_val}"
assert feat_val == 10, f"Expected feat_xy_from_raw=10, got {feat_val}"
INFO(f"  Phase 1 OK: loaded DB_raw(x={x_val},z={z_val}), DB_feat(feat_xy={feat_val})")

assert db_raw.is_frozen(), "DB_raw should be frozen from Run 1"
INFO("  Phase 1b OK: DB_raw confirmed frozen")

master.launch_local_workers([{}])
assert master.wait_for_workers(), \
    "Phase 2: Worker should connect"
INFO("  Phase 2 OK: 1 worker launched")

add_alpha_property(db_feat, "prop_signal", 1)
assert wait_for(lambda: len(master.completed_tasks) >= 1), \
    f"Phase 3: expected 1 completed, got {len(master.completed_tasks)}"
INFO("  Phase 3 OK: Worker gained 'alpha' property")

write_data(db_feat, "new_data", 99)

cross_db_sum(db_model, db_feat, db_feat, "feat_xy_from_raw", "new_data", "model_out")

assert wait_for(lambda: len(master.completed_tasks) >= 3), \
    f"Phase 4: expected 3 completed, got {len(master.completed_tasks)}"
INFO("  Phase 4 OK: cross-DB sum scheduled (async dependency)")

alpha_cross_db_copy(db_model, db_feat, "feat_xy_from_raw", "alpha_result")

gpu_cross_db_copy(db_model, db_feat, "new_data", "gpu_result")

assert wait_for(lambda: len(master.completed_tasks) >= 4), \
    f"Phase 5: expected 4 completed, got {len(master.completed_tasks)}"

assert wait_for(lambda: len(master.failed_tasks) >= 1), \
    f"Phase 5: expected 1 failed, got {len(master.failed_tasks)}"
INFO("  Phase 5 OK: alpha task completed, gpu task failed")

triple_db_sum(db_model, db_raw, db_feat, "x", "new_data", "triple_out")

assert wait_for(lambda: len(master.completed_tasks) >= 5), \
    f"Phase 6: expected 5 completed, got {len(master.completed_tasks)}"
INFO("  Phase 6 OK: triple-DB compute done")

log_dir = get_config().get_str("log_dir")
failed_file = os.path.join(log_dir, "failed_tasks.bin")

master.launch_local_workers([{"attributes": ["gpu"]}])
assert master.wait_for_workers(), \
    "Phase 7: gpu worker should connect"

master.restart_failed_tasks(failed_file)

assert wait_for(lambda: len(master.completed_tasks) >= 6), \
    f"Phase 7: expected 6 completed, got {len(master.completed_tasks)}"
assert wait_for(lambda: len(master.failed_tasks) == 0), \
    f"Phase 7: expected 0 failed, got {len(master.failed_tasks)}"
INFO("  Phase 7 OK: gpu task restarted and completed")

model_out = db_model.read_object("model_out")
alpha_result = db_model.read_object("alpha_result")
gpu_result = db_model.read_object("gpu_result")
triple_out = db_model.read_object("triple_out")

assert model_out == 109, f"Expected model_out=109 (10+99), got {model_out}"
assert alpha_result == 10, f"Expected alpha_result=10, got {alpha_result}"
assert gpu_result == 99, f"Expected gpu_result=99, got {gpu_result}"
assert triple_out == 109, f"Expected triple_out=109 (10+99), got {triple_out}"

new_data = db_feat.read_object("new_data")
assert new_data == 99, f"Expected new_data=99, got {new_data}"

INFO(f"  Phase 8 OK: all data verified — model_out={model_out}, alpha={alpha_result}, "
      f"gpu={gpu_result}, triple={triple_out}")

INFO("[PASS] Run 2 complete — all 12 checks passed")
