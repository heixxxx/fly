"""Run 1: write data, verify value, exit for Run 2 to load_db and rerun."""
import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_provenance_load_db_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db, get_config


def wait_for(condition, timeout=30.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1), \
    "Worker should connect"

db = open_db(DB_PATH)

write_data(db, "prov_key", 42)

assert wait_for(lambda: len(master.completed_tasks) >= 1), \
    f"Write should complete, got {len(master.completed_tasks)}"

val = db.read_object("prov_key")
assert val == 42, f"Expected 42, got {val}"

print("[RUN1] Written prov_key=42, exiting for Run 2", file=sys.stderr)
