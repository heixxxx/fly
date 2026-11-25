"""run3：load_project 新路径 → 集中后的数据全部可读。"""
import os

from fly import load_project
from test import record
from fly.runtime import get_agent

NEW_PATH = os.environ["FLY_NEW_PROJ_PATH"]

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(1)

proj = load_project(NEW_PATH)
db = proj.get_db("workdb")
for i in range(10):
    v = db.read_object(f"obj_{i}")
    # 原断言：assert v["i"] == i, f"obj_{i} corrupted: {v}"
    record(f"consol/obj_{i}", v["i"])

master.stop()
print("[DONE] migrate_consol_run3: all objects readable after consolidate+migrate")
