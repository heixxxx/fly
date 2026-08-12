"""Project Phase 3: 同步 freeze 管理 + 边界（freeze_db/freeze_all/is_db_frozen/wait_frozen/get_db KeyError）。

sub case（fly 进程跑），从原 test_project.py 的 test_sync_freeze_and_edges 提取。
"""
import os, shutil, time
from _fly_log import INFO, WARN
from fly import launch_workers, as_task
from fly.runtime import get_agent
from fly.project import register_flow
from test.py.demo_project import _write_val_task, _demo_freeze_task, DemoProject as _DP

PATH = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "project_sync")
if os.path.isdir(PATH):
    shutil.rmtree(PATH, ignore_errors=True)

launch_workers([{}])
assert get_agent().wait_for_workers(1), "1 worker should connect"


def _wait_completed(n, timeout=30.0):
    agent = get_agent()
    t0 = time.time()
    while time.time() - t0 < timeout:
        if len(agent.completed_tasks) >= n:
            return True
        time.sleep(0.5)
    return False


@register_flow(_DP)
def make_db_unfrozen(self, name, value):
    """建库 + 写值，但不 freeze（用于测试同步 freeze 管理方法）。"""
    db = self._create_db(name)
    _write_val_task(db, value, None)
    return db


proj = _DP(PATH)
db_a = proj.make_db_unfrozen(name="db_a", value=1)
assert _wait_completed(1), "db_a write task should complete"

assert proj.is_db_frozen("db_a") is False, "db_a should not be frozen yet"
assert proj.is_db_frozen("nope") is False, "unresolved name should return False"
assert proj.wait_frozen("db_a", timeout=0.1) is False, "wait_frozen should time out"
assert proj.wait_frozen("nope", timeout=0.1) is False

proj.freeze_db("db_a")
assert proj.is_db_frozen("db_a") is True, "freeze_db should freeze db_a"

db_b = proj.make_db_unfrozen(name="db_b", value=2)
assert _wait_completed(2), "db_b write task should complete"
assert proj.is_db_frozen("db_b") is False, "db_b unfrozen before freeze_all"
proj._db_cache.pop("db_b", None)  # force cache-miss → load_db path
proj.freeze_all()
assert proj.is_db_frozen("db_b") is True, "freeze_all should freeze db_b (load_db path)"
assert proj.get_db("db_b").read_object("val") == 2, "load_db-restored db_b has data"

raised = False
try:
    proj.get_db("does_not_exist")
except KeyError:
    raised = True
assert raised, "get_db on missing name should raise KeyError"

@register_flow(_DP)
def make_db_unfrozen(self, name, value):  # noqa: F811 override → WARN
    return self._create_db(name)
assert "make_db_unfrozen" in _DP._flows
INFO("[PASS] test_sync_freeze_and_edges")
get_agent().stop()
