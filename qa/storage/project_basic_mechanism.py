"""Project Phase 1: 基类机制（注册 / _create_db / get_db / freeze / 持久化 / 重名 / 显式传 db / pickle）。

sub case（fly 进程跑），从原 test_project.py 的 test_basic_mechanism 提取。
路径用 FLY_CASE_LOG_DIR（.pyt 注入，case log 目录）。
"""
import os, shutil, pickle, json
from _fly_log import INFO
from fly import launch_workers
from fly.runtime import get_agent
from test import DemoProject, make_db  # noqa: F401（make_db 触发 flow 注册）

PROJ_PATH = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "project_basic")
if os.path.isdir(PROJ_PATH):
    shutil.rmtree(PROJ_PATH, ignore_errors=True)

launch_workers([{}])
assert get_agent().wait_for_workers(1), "1 worker should connect"

proj = DemoProject(PROJ_PATH)
assert proj.list_flows() == ["make_db"], f"flows={proj.list_flows()}"
assert proj.list_dbs() == []

db1 = proj.make_db(name="step1", value=100)
assert "step1" in proj.list_dbs()
assert proj.wait_frozen("step1", timeout=60), "step1 should freeze"
assert db1.read_object("val") == 100

db1b = proj.get_db("step1")
assert db1b.get_db_path() == db1.get_db_path()

# 重名 → WARN + 递增
proj.make_db(name="step1", value=200)
assert proj.wait_frozen("step1", timeout=60, latest=True), "step1.1 should freeze"
assert proj.get_db("step1").read_object("val") == 100, \
    "get_db('step1') should match exact 'step1', not .1"
assert proj.get_db("step1.1").read_object("val") == 200
assert proj.get_db("step1", latest=True).read_object("val") == 200

# 显式传 db 作输入
proj.make_db(name="step2", value=None, src_db=proj.get_db("step1", latest=True))
assert proj.wait_frozen("step2", timeout=60), "step2 should freeze"
assert proj.get_db("step2").read_object("from_src") == 200

# meta 持久化
meta_path = os.path.join(PROJ_PATH, "_PROJECT_META.json")
assert os.path.isfile(meta_path)
with open(meta_path) as f:
    meta = json.load(f)
assert meta["class"].endswith("DemoProject")
assert meta["project_id"]
assert len(meta["dbs"]) >= 3

# 已存在 project 重新绑定
proj2 = DemoProject(PROJ_PATH)
assert proj2.list_dbs() == ["step1", "step1.1", "step2"], f"list_dbs={proj2.list_dbs()}"

# pickle 支持
blob = pickle.dumps(proj2)
proj3 = pickle.loads(blob)
assert proj3.db_path == proj2.db_path
assert proj3._db_cache == {}

assert "DemoProject" in repr(proj)
get_agent().stop()
INFO("[PASS] test_basic_mechanism")
