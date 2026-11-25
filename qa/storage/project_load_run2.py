"""Phase 2 Run 2: load_project 全量恢复 + 子类还原 + 读对象。

验证：
  - fly.load_project 读 _PROJECT_META.json，动态还原成 DemoProject 子类
  - 注册的 flow（make_db）在恢复后的实例上可用
  - db 索引已全量 load，可读回对象
  - db_path 不变（与 run1 一致）
"""
import os

import fly
from _fly_log import INFO
from test import record

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

# 编排脚本挪入的检查（原先在两个 sub case 之间内联断言，重录模式会真的执行
# 内联语句而失败，见 qa/README.md「编排脚本的写法约定」）：时机不变——run1
# 的工作刚结束、本 sub case 开始工作之前。
# 原断言：assert os.path.isfile(os.path.join(PROJ_PATH, "_PROJECT_META.json")), \
record("meta_file_exists", os.path.isfile(os.path.join(PROJ_PATH, "_PROJECT_META.json")),
       detail=PROJ_PATH)

proj = fly.load_project(PROJ_PATH)

# 子类动态还原：应是 DemoProject，flow 可用。
# 原断言：assert isinstance(proj, DemoProject), \
record("restored_class", type(proj).__name__)
# 原断言：assert "make_db" in proj.list_flows(), \
record("flows", proj.list_flows())

# get_db 读回库。
db = proj.get_db("step1")
# 原断言：assert db.read_object("val") == 12345, "val should be readable after load"
record("val", db.read_object("val"))

# db_path 不变。
with open(os.path.join(PROJ_PATH, "_run1_db_path")) as f:
    run1_db_path = f.read().strip()
# 原断言：assert db.get_db_path() == run1_db_path, \
record("db_path_unchanged", {"run1": run1_db_path, "run2": db.get_db_path()})

INFO(f"[RUN2] load_project ok: class={type(proj).__name__}, "
     f"flows={proj.list_flows()}, db_path={db.get_db_path()}")
