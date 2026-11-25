"""Project Phase 1: 基类机制（注册 / _create_db / get_db / freeze / 持久化 / 重名 / 显式传 db / pickle）。

sub case（fly 进程跑），从原 test_project.py 的 test_basic_mechanism 提取。
路径用 FLY_CASE_LOG_DIR（.pyt 注入，case log 目录）。
"""
import os, shutil, pickle, json
from _fly_log import INFO
from fly import launch_workers
from fly.runtime import get_agent
from test import DemoProject, make_db, record  # noqa: F401（make_db 触发 flow 注册）

PROJ_PATH = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "project_basic")
if os.path.isdir(PROJ_PATH):
    shutil.rmtree(PROJ_PATH, ignore_errors=True)

launch_workers([{}])
assert get_agent().wait_workers_registered(timeout=60.0), "1 worker should connect"

proj = DemoProject(PROJ_PATH)
# 原断言：assert proj.list_flows() == ["make_db"], f"flows={proj.list_flows()}"
record("flows", proj.list_flows())
# 原断言：assert proj.list_dbs() == []
record("dbs_initial", proj.list_dbs())

db1 = proj.make_db(name="step1", value=100)
# 原断言：assert "step1" in proj.list_dbs()
record("dbs_after_step1", proj.list_dbs())
assert proj.wait_frozen("step1", timeout=60), "step1 should freeze"
# 原断言：assert db1.read_object("val") == 100
record("step1_val", db1.read_object("val"))

db1b = proj.get_db("step1")
# 原断言：assert db1b.get_db_path() == db1.get_db_path()
record("get_db_same_path", {"db1": db1.get_db_path(), "db1b": db1b.get_db_path()})

# 重名 → WARN + 递增
proj.make_db(name="step1", value=200)
assert proj.wait_frozen("step1", timeout=60, latest=True), "step1.1 should freeze"
# 原断言：assert proj.get_db("step1").read_object("val") == 100, \
record("step1_exact_val", proj.get_db("step1").read_object("val"))
# 原断言：assert proj.get_db("step1.1").read_object("val") == 200
record("step1_1_val", proj.get_db("step1.1").read_object("val"))
# 原断言：assert proj.get_db("step1", latest=True).read_object("val") == 200
record("step1_latest_val", proj.get_db("step1", latest=True).read_object("val"))

# 显式传 db 作输入
proj.make_db(name="step2", value=None, src_db=proj.get_db("step1", latest=True))
assert proj.wait_frozen("step2", timeout=60), "step2 should freeze"
# 原断言：assert proj.get_db("step2").read_object("from_src") == 200
record("step2_from_src_val", proj.get_db("step2").read_object("from_src"))

# meta 持久化
meta_path = os.path.join(PROJ_PATH, "_PROJECT_META.json")
# 原断言：assert os.path.isfile(meta_path)
record("meta_file_exists", os.path.isfile(meta_path), detail=meta_path)
with open(meta_path) as f:
    meta = json.load(f)
# 原断言：assert meta["class"].endswith("DemoProject")
record("meta_class_suffix", meta["class"].endswith("DemoProject"), detail=meta["class"])
# 原断言：assert meta["project_id"]
record("meta_project_id_set", bool(meta["project_id"]))
# 原断言：assert len(meta["dbs"]) >= 3
record("meta_dbs_at_least_3", len(meta["dbs"]) >= 3, detail=len(meta["dbs"]))

# 已存在 project 重新绑定
proj2 = DemoProject(PROJ_PATH)
# 原断言：assert proj2.list_dbs() == ["step1", "step1.1", "step2"], f"list_dbs={proj2.list_dbs()}"
record("dbs_rebind", proj2.list_dbs())

# pickle 支持
blob = pickle.dumps(proj2)
proj3 = pickle.loads(blob)
# 原断言：assert proj3.db_path == proj2.db_path
record("pickle_db_path_preserved", {"proj2": proj2.db_path, "proj3": proj3.db_path})
# 原断言：assert proj3._db_cache == {}
record("pickle_db_cache_empty", proj3._db_cache)

# 原断言：assert "DemoProject" in repr(proj)
record("repr_contains_class", "DemoProject" in repr(proj), detail=repr(proj))
get_agent().stop()
INFO("[DONE] test_basic_mechanism")
