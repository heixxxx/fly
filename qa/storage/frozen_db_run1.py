"""Run 1 of frozen DB load test. Create DB, write data via tasks, freeze."""
from _fly_log import INFO
import os
import sys
import time
import shutil

DB_PATH = "/tmp/fly_e2e_frozen_load_db"

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from e2e_tasks import write_data
from fly import open_db
from fly import get_config
from fly.runtime import get_agent


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

write_data(db, "frozen/a", 10)
write_data(db, "frozen/b", 20)
write_data(db, "frozen/c", "hello")

completed = master.wait_for_all_tasks(expected=3, timeout=30)
assert len(completed) >= 3, f"Expected 3 completed tasks, got {len(completed)}"

time.sleep(0.5)

assert db.read_object("frozen/a") == 10
assert db.read_object("frozen/b") == 20
assert db.read_object("frozen/c") == "hello"

db.freeze()
assert db.is_frozen(), "DB should be frozen after freeze()"

assert os.path.isfile(os.path.join(DB_PATH, "_FROZEN")), "_FROZEN marker should exist"

INFO(f"[RUN1] Created DB, wrote 3 objects, frozen successfully")
