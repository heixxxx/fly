"""Phase 2 Run 2: load_project 全量恢复 + 子类还原 + 读对象。

验证：
  - fly.load_project 读 _PROJECT_META.json，动态还原成 DemoProject 子类
  - 注册的 flow（make_db）在恢复后的实例上可用
  - db 索引已全量 load，可读回对象
  - db_path 不变（与 run1 一致）
"""
import os
import sys

import fly
from demo_project import DemoProject

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

proj = fly.load_project(PROJ_PATH)

# 子类动态还原：应是 DemoProject，flow 可用。
assert isinstance(proj, DemoProject), \
    f"load_project should restore DemoProject, got {type(proj).__name__}"
assert "make_db" in proj.list_flows(), \
    f"flow make_db should be available, flows={proj.list_flows()}"

# get_db 读回库。
db = proj.get_db("step1")
assert db.read_object("val") == 12345, "val should be readable after load"

# db_path 不变。
with open(os.path.join(PROJ_PATH, "_run1_db_path")) as f:
    run1_db_path = f.read().strip()
assert db.get_db_path() == run1_db_path, \
    f"db_path changed: run1={run1_db_path}, run2={db.get_db_path()}"

print(f"[RUN2] load_project ok: class={type(proj).__name__}, "
      f"flows={proj.list_flows()}, db_path={db.get_db_path()}", file=sys.stderr)
