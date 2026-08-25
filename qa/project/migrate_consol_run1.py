"""run1：hostA worker 写数据（模拟数据分散在远端 host）→ freeze。"""
import os
import shutil
import time

from fly import as_task, open_project
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

if os.path.isdir(PROJ_PATH):
    shutil.rmtree(PROJ_PATH, ignore_errors=True)

master = get_agent()
master.launch_local_workers([{"host": "consol-host-a"}])
assert master.wait_for_workers(1)

proj = open_project(PROJ_PATH)
db = proj._create_db("workdb")


@as_task()
def write_objs(db):
    for i in range(10):
        db.write_object(f"obj_{i}", {"i": i, "payload": "x" * 1024})


write_objs(db)
assert master.wait_for_all_tasks(timeout=60)

db.freeze()
t0 = time.time()
while not proj.is_db_frozen("workdb") and time.time() - t0 < 30:
    time.sleep(0.2)
assert proj.is_db_frozen("workdb")

master.stop()
