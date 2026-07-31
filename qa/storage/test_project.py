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


# ── Phase 3: 同步 freeze 管理 + 边界 + load 错误 ───────────────────

def test_sync_freeze_and_edges():
    """覆盖同步 freeze 管理 (freeze_db / freeze_all) + 未解析/超时边界 + get_db KeyError。

    这些路径在 test_basic_mechanism 里未触及：那里走的是 flow 异步 freeze；
    这里用 make_db flow 造数据，但在 freeze 落定前/后断言同步管理方法与边界返回值。

    关键：未冻结的 db 必须通过 flow 造数据（worker 才能写入），所以用一个"只写
    不 freeze"的 flow（make_db_unfrozen）制造未冻结状态，再驱动 freeze_db/freeze_all。
    """
    from _fly_log import INFO, WARN
    from fly import launch_workers, as_task
    from fly.runtime import get_agent
    from fly.project import register_flow

    path = os.path.join(LOG_DIR, "project_sync")
    cleanup(path)
    launch_workers([{}])
    assert get_agent().wait_for_workers(1), "1 worker should connect"

    # 一个"只写不 freeze"的 flow：复用 demo_project 的 _write_val_task 写数据，
    # 但不提交 freeze task，从而留下一个有数据、未冻结的 db。
    from demo_project import _write_val_task, _demo_freeze_task, DemoProject as _DP

    @register_flow(_DP)
    def make_db_unfrozen(self, name, value):
        """建库 + 写值，但不 freeze（用于测试同步 freeze 管理方法）。"""
        db = self._create_db(name)
        _write_val_task(db, value, None)
        return db

    proj = _DP(path)
    db_a = proj.make_db_unfrozen(name="db_a", value=1)
    assert _wait_completed(1), "db_a write task should complete"

    # is_db_frozen 对未冻结的 db → False（覆盖 confirmed ∪ pending 都为空的真路径）。
    assert proj.is_db_frozen("db_a") is False, "db_a should not be frozen yet"
    # is_db_frozen 对未解析的 name → False（L233-235 短路分支）。
    assert proj.is_db_frozen("nope") is False, "unresolved name should return False"
    # wait_frozen 对未冻结 db + 极短 timeout → False（L261-265 超时分支）。
    assert proj.wait_frozen("db_a", timeout=0.1) is False, \
        "wait_frozen should time out (False) on an unfrozen db"
    # wait_frozen 对未解析 name → False（L257-259 短路）。
    assert proj.wait_frozen("nope", timeout=0.1) is False

    # freeze_db：同步冻结（L207-217，整方法此前未测）。阻塞到完成。
    proj.freeze_db("db_a")
    assert proj.is_db_frozen("db_a") is True, "freeze_db should freeze db_a"

    # freeze_all：再造第二个未冻结 db，freeze_all 应把所有未冻结库一并冻结。
    # 关键：把 db_b 从缓存移除，强制走 cache-miss → load_db 分支（L274-278）。
    # 此前 freeze_all 在该分支误用 open_db（会递增创建空库，freeze 对原 db_id
    # 无效）；现已改用 load_db，此处回归该修复。
    db_b = proj.make_db_unfrozen(name="db_b", value=2)
    assert _wait_completed(2), "db_b write task should complete"
    assert proj.is_db_frozen("db_b") is False, "db_b should be unfrozen before freeze_all"
    proj._db_cache.pop("db_b", None)   # force the cache-miss -> load_db path
    proj.freeze_all()
    assert proj.is_db_frozen("db_b") is True, "freeze_all should freeze db_b (load_db path)"
    # load_db 恢复的句柄应能读到原数据（证明冻结的是真库，非 open_db 的空库）。
    assert proj.get_db("db_b").read_object("val") == 2, \
        "load_db-restored db_b should still have its written data"

    # get_db KeyError（L192-194）：请求不存在的 name 应抛 KeyError。
    raised = False
    try:
        proj.get_db("does_not_exist")
    except KeyError:
        raised = True
    assert raised, "get_db on a missing name should raise KeyError"

    # register_flow override WARN（L50-53）：在同一类上重复注册同名 flow 应 WARN。
    @register_flow(_DP)
    def make_db_unfrozen(self, name, value):  # noqa: F811 (intentional override)
        """Override to exercise the WARN-on-existing branch."""
        return self._create_db(name)
    assert "make_db_unfrozen" in _DP._flows, "re-registered flow should be in _flows"
    INFO("[PASS] test_sync_freeze_and_edges")

    get_agent().stop()


def _wait_completed(n, timeout=30.0):
    """Poll the current master's completed-task count to >= n."""
    import time as _t
    from fly.runtime import get_agent
    agent = get_agent()
    t0 = _t.time()
    while _t.time() - t0 < timeout:
        if len(agent.completed_tasks) >= n:
            return True
        _t.sleep(0.5)
    return False


def test_load_project_errors():
    """覆盖 Project.load 的错误分支：无 meta → RuntimeError。"""
    from _fly_log import INFO

    # 无 _PROJECT_META.json 的目录 → RuntimeError（L317-318）。
    empty = os.path.join(LOG_DIR, "project_empty")
    cleanup(empty)
    os.makedirs(empty, exist_ok=True)
    raised = False
    try:
        Project.load(empty)
    except RuntimeError as e:
        raised = "_PROJECT_META" in str(e)
    assert raised, "Project.load on a dir without meta should raise RuntimeError"
    INFO("[PASS] test_load_project_errors")


test_sync_freeze_and_edges()
print(file=sys.stderr)
test_load_project_errors()
print("\nAll Project E2E tests passed!", file=sys.stderr)
