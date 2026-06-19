from _fly_log import INFO
import time
import os
import shutil
import glob



from e2e_tasks import write_temp
from fly import open_db, get_config
DB_PATH = os.path.join(get_config().get_str("log_dir"), "db")


def cleanup():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)


def wait_for(condition, timeout=60.0, interval=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


def get_temp_disk_bytes():
    total = 0
    for d in glob.glob("/tmp/fly_temp_*"):
        if os.path.isdir(d):
            for root, dirs, files in os.walk(d):
                for f in files:
                    total += os.path.getsize(os.path.join(root, f))
    return total


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 0)

from fly.runtime import get_agent
master = get_agent()

master.launch_local_workers([{}])
assert master.wait_for_workers(1)

db = open_db(DB_PATH)

write_temp(db, "d1", "A" * 500)
write_temp(db, "d2", "B" * 500)
write_temp(db, "d3", "C" * 500)

assert wait_for(lambda: len(master.completed_tasks) >= 3, timeout=30.0)
assert len(master.failed_tasks) == 0

assert db.read_object("d1") == "A" * 500
assert db.read_object("d2") == "B" * 500
assert db.read_object("d3") == "C" * 500

disk_before = get_temp_disk_bytes()

db.remove_object("d1")
db.remove_object("d2")
db.remove_object("d3")

disk_after = get_temp_disk_bytes()

assert disk_after <= disk_before, \
    f"Disk should not grow after remove: before={disk_before}, after={disk_after}"

try:
    db.read_object("d1")
    assert False, "Should fail after remove"
except Exception:
    pass

INFO(f"[PASS] test_save_to_db_false_remove: disk before={disk_before}, after={disk_after}")
