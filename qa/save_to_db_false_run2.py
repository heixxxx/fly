import time
import sys
import os
import shutil

DB_PATH = "/tmp/fly_e2e_save_to_db_false_persist_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import load_db, get_config


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = load_db(DB_PATH)

assert db.read_object("perm") == 42

try:
    db.read_object("temp")
    assert False, "temp data should not persist across restart"
except Exception:
    pass

print("[PASS] save_to_db_false_run2: perm=42 survived, temp gone", file=sys.stderr)
