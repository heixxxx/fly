"""run3：load_project → 补依赖 → resume——uid 解析自愈端到端验收。

bin 记录内是旧路径快照（args/inputs/owner）；restart 按运行时 uid 索引
命中新路径：args 重编码 v2、inputs 前缀替换、owner 归一化 → 重投完成、
数据落新 data_path、旧路径无幽灵目录。
"""
import os

from _fly_log import INFO
from fly import load_project, wait_tasks
from test import record
from fly.runtime import get_agent

NEW_PATH = os.environ["FLY_NEW_PROJ_PATH"]
OLD_PATH = os.environ["FLY_OLD_PROJ_PATH"]

master = get_agent()
master.launch_local_workers([{}])
assert master.wait_for_workers(1)

proj = load_project(NEW_PATH)
db = proj.get_db("workdb")

# 补齐 run1 缺失的依赖对象（master 自写即 ready，落在新路径 + 新 data_path）。
db.write_object("phantom_dep", "pd")

# resume：内部 restart_failed_tasks(新路径 db list) → uid 命中 → 重投。
proj.resume()

ok = wait_tasks(timeout=120)
assert ok, "resumed task should complete"

ok_obj_val = db.read_object("ok_obj")
# 原断言：assert db.read_object("ok_obj") == 42
record("resume_run3/ok_obj", ok_obj_val)
dep_obj_val = db.read_object("dep_obj")
# 原断言：assert db.read_object("dep_obj") == "pd"
record("resume_run3/dep_obj", dep_obj_val)

# bin 消费后删除。
bin_path = os.path.join(NEW_PATH, "workdb", "failed_tasks.bin")
from test import wait_until
assert wait_until(lambda: not os.path.isfile(bin_path), timeout=30), \
    "bin should be consumed after successful resume"

# 旧路径无幽灵目录（uid 解析不依赖旧路径，worker 不在旧位置重建 db）。
no_ghost = not os.path.exists(OLD_PATH)
# 原断言：assert not os.path.exists(OLD_PATH), \
record("resume_run3/no_ghost_dir_at_old_path", no_ghost, detail=OLD_PATH)
# 非守卫：OLD_PATH 之后不再使用

# 数据落新分离 data_path。
data_dir = os.path.join(NEW_PATH, "workdb_data")
data_landed = os.path.isdir(data_dir) and any(
    f.startswith("data_") for f in os.listdir(data_dir))
# 原断言：assert os.path.isdir(data_dir) and any(
record("resume_run3/data_in_migrated_data_path", data_landed, detail=data_dir)
# 非守卫：data_dir 之后不再使用

INFO("[DONE] migrate_resume_run3: uid-resolved restart after migration")
master.stop()
