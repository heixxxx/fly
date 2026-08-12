"""Run 2: load_db, submit same write_data(db, key, 42), verify idempotent."""
from _fly_log import INFO
import time
import os



from test import write_data
from fly import load_db, get_config
DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

db = load_db(DB_PATH)

assert master.wait_for_workers(1), \
    "load_db should spawn worker"

val = db.read_object("prov_key")
assert val == 42, f"load_db should restore data, expected 42 got {val}"

write_data(db, "prov_key", 42)

assert wait_for(lambda: len(master.completed_tasks) >= 1, timeout=30.0), \
    f"Rerun should succeed, got {len(master.completed_tasks)} completed, {len(master.failed_tasks)} failed"

assert len(master.failed_tasks) == 0, \
    f"Expected 0 failed, got {len(master.failed_tasks)}"

val2 = db.read_object("prov_key")
assert val2 == 42, f"Expected 42 after rerun, got {val2}"

# Part B: load_db 从 idx 重建 write_provenance_。用不同 context（value=999）写 prov_key，
# 应被 provenance 拒（mismatch）—— 重建后 hash 与原 42 的 context 不匹配。
write_data(db, "prov_key", 999)
assert wait_for(lambda: len(master.failed_tasks) >= 1, timeout=30.0), \
    f"不同 context 写应被重建的 provenance 拒（mismatch），got {len(master.failed_tasks)} failed"

INFO("[RUN2] load_db rebuilds provenance: idempotent + mismatch verified")
