"""Phase 2 Run 1: 用 DemoProject.make_db 建库写数据 freeze（异步范式）。

验证：flow 异步提交 task → wait_frozen 等完成 → _PROJECT_META.json 含 class 字段。
用 DemoProject（demo_project.py 独立模块，worker 可 import），以便 run2 验证
load_project 的子类动态还原。
"""
import os
import sys

from fly import get_config, launch_workers
from fly.runtime import get_agent
from test import DemoProject

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

# make_db flow 是异步范式（task 执行），用户负责唤起 worker。
launch_workers([{}])
assert get_agent().wait_for_workers(1), "1 worker should connect"

proj = DemoProject(PROJ_PATH)
proj.make_db(name="step1", value=12345)
# wait_frozen 等 freeze task 完成（val 写完 + freeze）。
assert proj.wait_frozen("step1", timeout=60), "step1 should freeze"

db = proj.get_db("step1")
assert db.read_object("val") == 12345

# 记录 db_path 供 run2 比对（load 后 db_path 应不变）。
with open(os.path.join(PROJ_PATH, "_run1_db_path"), "w") as f:
    f.write(db.get_db_path())

get_agent().stop()
print(f"[RUN1] make_db done: db_path={db.get_db_path()}, val=12345", file=sys.stderr)
