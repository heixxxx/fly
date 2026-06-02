"""Run 2: load_db, submit same write_data(db, key, 42), verify idempotent."""
import time
import sys
import os

DB_PATH = "/tmp/fly_e2e_provenance_load_db_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import load_db, get_config


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

print("[RUN2] load_db + rerun idempotent, verified", file=sys.stderr)
