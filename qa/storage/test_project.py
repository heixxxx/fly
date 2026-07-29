"""E2E test: fly.Project 基类机制（注册 / 建库 / 取库 / 冻结 / 持久化 / load 恢复）。

验证内容（设计见 docs/project-design.md）：
  Phase 1（单进程 master，不开 worker）：注册机制 + _create_db + get_db + freeze
           + 重名 WARN 递增 + 显式传 db + 持久化 + repr + pickle
  Phase 2（两进程）：load_project 全量恢复 + 子类还原 + 读对象

Project 基类的管理逻辑不依赖 worker（open_db 在 master 模式走 get_or_create_database，
master 自写自读）；load_project 内部用 fly.load_db 需 worker，故 Phase 2 走子进程。
"""
import os
import sys
import pickle
import shutil
import subprocess

from fly import get_fly_binary, get_config
from fly.project import Project, register_flow
from demo_project import DemoProject, make_db   # noqa: F401 (make_db triggers registration on import)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
FLY_BIN = get_fly_binary()

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "project_basic")        # Phase 1
PROJ_PATH_LOAD = os.path.join(LOG_DIR, "project_load")    # Phase 2 run1 写入处


def cleanup(path):
    if os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


def _merge_env(extra):
    e = os.environ.copy()
    if extra:
        e.update(extra)
    return e


def run_script(script_name, log_dir, extra_env=None):
    """Run a Python script via the fly binary in a subprocess."""
    script_path = os.path.join(SCRIPT_DIR, script_name)
    os.makedirs(log_dir, exist_ok=True)
    return subprocess.run(
        [FLY_BIN, "--log-dir", log_dir, script_path],
        capture_output=True, text=True, timeout=120,
        cwd=PROJECT_ROOT, env=_merge_env(extra_env),
    )


# 一个测试用的子类 + flow 定义在 _demo_project.py（独立模块，便于 pickle）。

# ── Phase 1: 单进程基类机制 ──────────────────────────────────────

def test_basic_mechanism():
    """注册 / _create_db / get_db / freeze-as-task / 持久化 / 重名 / 显式传 db / pickle。"""
    from _fly_log import INFO
    from fly import launch_workers
    from fly.runtime import get_agent

    cleanup(PROJ_PATH)

    # make_db flow 现在是异步范式（task 执行），需 worker。用户负责唤起。
    launch_workers([{}])
    assert get_agent().wait_for_workers(1), "1 worker should connect"

    proj = DemoProject(PROJ_PATH)
    assert proj.list_flows() == ["make_db"], f"flows={proj.list_flows()}"
    assert proj.list_dbs() == []

    # flow 异步返回 db（提交入口 task + freeze task 后立即返回，不等）。
    db1 = proj.make_db(name="step1", value=100)
    assert "step1" in proj.list_dbs()
    # wait_frozen 等待 freeze task 完成（即 val 写完 + freeze）。
    assert proj.wait_frozen("step1", timeout=60), "step1 should freeze"
    assert db1.read_object("val") == 100

    # get_db 精确匹配 actual_name（命中缓存）。
    db1b = proj.get_db("step1")
    assert db1b.get_db_id() == db1.get_db_id()

    # 重名 → WARN + 自动递增：第二次 make_db("step1") 实际建 step1.1。
    proj.make_db(name="step1", value=200)
    assert proj.wait_frozen("step1", timeout=60, latest=True), "step1.1 should freeze"
    # 新语义：get_db("step1") 仍精确匹配最初的 step1（val=100）；
    #         递增产物要显式 get_db("step1.1") 或 get_db("step1", latest=True)。
    assert proj.get_db("step1").read_object("val") == 100, \
        "get_db('step1') should match the exact 'step1', not the .1 variant"
    assert proj.get_db("step1.1").read_object("val") == 200, \
        "get_db('step1.1') should match the incremented variant"
    assert proj.get_db("step1", latest=True).read_object("val") == 200, \
        "get_db('step1', latest=True) should return the newest 'step1' variant"

    # 显式传 db 作输入（意见2）：step2 的 kickoff task 依赖 step1 最新版的 val。
    proj.make_db(name="step2", value=None, src_db=proj.get_db("step1", latest=True))
    assert proj.wait_frozen("step2", timeout=60), "step2 should freeze"
    assert proj.get_db("step2").read_object("from_src") == 200

    # meta 持久化：磁盘上有 _PROJECT_META.json，含 class / dbs。
    import json
    meta_path = os.path.join(PROJ_PATH, "_PROJECT_META.json")
    assert os.path.isfile(meta_path)
    with open(meta_path) as f:
        meta = json.load(f)
    assert meta["class"].endswith("DemoProject")
    assert meta["project_id"]
    assert len(meta["dbs"]) >= 3   # step1, step1.1, step2

    # 已存在 project 重新绑定（不重建目录）：读回 meta。
    proj2 = DemoProject(PROJ_PATH)
    # list_dbs 返回 actual_name（含递增产物）。
    assert proj2.list_dbs() == ["step1", "step1.1", "step2"], \
        f"list_dbs={proj2.list_dbs()}"

    # pickle 支持（作 task 参数传递）。
    blob = pickle.dumps(proj2)
    proj3 = pickle.loads(blob)
    assert proj3.base_path == proj2.base_path
    assert proj3._db_cache == {}

    # repr。
    assert "DemoProject" in repr(proj)
    get_agent().stop()
    INFO("[PASS] test_basic_mechanism")


# ── Phase 2: 两进程 load_project 全量恢复 ───────────────────────

def test_load_project_two_processes():
    """run1 建库写数据 freeze → run2 load_project 读回对象 + 子类还原。"""
    cleanup(PROJ_PATH_LOAD)
    log_dir = os.path.join(SCRIPT_DIR, "logs", "project_load")

    r1 = run_script("project_load_run1.py", os.path.join(log_dir, "run1"),
                    extra_env={"FLY_PROJ_PATH": PROJ_PATH_LOAD})
    sys.stderr.write(r1.stderr)
    if r1.returncode != 0:
        sys.stderr.write(r1.stdout)
        assert False, f"Run 1 failed (exit={r1.returncode})\n{r1.stderr}"

    meta_path = os.path.join(PROJ_PATH_LOAD, "_PROJECT_META.json")
    assert os.path.isfile(meta_path), "_PROJECT_META.json should exist after run1"

    r2 = run_script("project_load_run2.py", os.path.join(log_dir, "run2"),
                    extra_env={"FLY_PROJ_PATH": PROJ_PATH_LOAD})
    sys.stderr.write(r2.stderr)
    if r2.returncode != 0:
        sys.stderr.write(r2.stdout)
        assert False, f"Run 2 failed (exit={r2.returncode})\n{r2.stderr}"

    print("[PASS] test_load_project_two_processes", file=sys.stderr)


test_basic_mechanism()
print(file=sys.stderr)
test_load_project_two_processes()
print("\nAll Project E2E tests passed!", file=sys.stderr)
