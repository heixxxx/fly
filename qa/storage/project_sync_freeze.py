"""Project Phase 3: 同步 freeze 管理 + 边界（freeze_db/freeze_all/is_db_frozen/wait_frozen/get_db KeyError）。

sub case（fly 进程跑），从原 test_project.py 的 test_sync_freeze_and_edges 提取。
"""
import os, shutil, time
from _fly_log import INFO, WARN
from fly import launch_workers, as_task
from fly.runtime import get_agent
from fly.project import register_flow
from test.py.demo_project import _write_val_task, _demo_freeze_task, DemoProject as _DP
from test import record

PATH = os.path.join(os.environ["FLY_CASE_LOG_DIR"], "project_sync")
if os.path.isdir(PATH):
    shutil.rmtree(PATH, ignore_errors=True)

launch_workers([{}])
assert get_agent().wait_workers_registered(timeout=60.0), "1 worker should connect"


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
# 保留：等待门槛——_wait_completed 超时返回假时，后面的 freeze 语义检查全部
# 失去前提；等待语义留在脚本里，不转记录。
assert _wait_completed(1), "db_a write task should complete"  # 保留：等待门槛

# 原断言：assert proj.is_db_frozen("db_a") is False, "db_a should not be frozen yet"
record("db_a_not_frozen_before_freeze", proj.is_db_frozen("db_a") is False,
       detail=proj.is_db_frozen("db_a"))
# 原断言：assert proj.is_db_frozen("nope") is False, "unresolved name should return False"
record("unresolved_name_not_frozen", proj.is_db_frozen("nope") is False,
       detail=proj.is_db_frozen("nope"))
assert proj.wait_frozen("db_a", timeout=0.1) is False, "wait_frozen should time out"
assert proj.wait_frozen("nope", timeout=0.1) is False

proj.freeze_db("db_a")
# 原断言：assert proj.is_db_frozen("db_a") is True, "freeze_db should freeze db_a"
record("db_a_frozen_after_freeze_db", proj.is_db_frozen("db_a") is True,
       detail=proj.is_db_frozen("db_a"))

db_b = proj.make_db_unfrozen(name="db_b", value=2)
# 保留：等待门槛——同上，等不到 db_b 写完则后续 freeze_all 检查失去前提。
assert _wait_completed(2), "db_b write task should complete"  # 保留：等待门槛
# 原断言：assert proj.is_db_frozen("db_b") is False, "db_b unfrozen before freeze_all"
record("db_b_not_frozen_before_freeze_all", proj.is_db_frozen("db_b") is False,
       detail=proj.is_db_frozen("db_b"))
proj._db_cache.pop("db_b", None)  # force cache-miss → load_db path
proj.freeze_all()
# 原断言：assert proj.is_db_frozen("db_b") is True, "freeze_all should freeze db_b (load_db path)"
record("db_b_frozen_after_freeze_all", proj.is_db_frozen("db_b") is True,
       detail=proj.is_db_frozen("db_b"))
# 原断言：assert proj.get_db("db_b").read_object("val") == 2, "load_db-restored db_b has data"
record("db_b_val", proj.get_db("db_b").read_object("val"))

get_db_raised = None
try:
    proj.get_db("does_not_exist")
except KeyError as e:
    get_db_raised = {"type": type(e).__name__, "msg": str(e)}
# 原断言：assert raised, "get_db on missing name should raise KeyError"
record("get_db_missing_raises", get_db_raised is not None, detail=get_db_raised)
record("get_db_missing_error_type", get_db_raised["type"] if get_db_raised else None)

@register_flow(_DP)
def make_db_unfrozen(self, name, value):  # noqa: F811 override → WARN
    return self._create_db(name)
# 原断言：assert "make_db_unfrozen" in _DP._flows
record("flows", sorted(_DP._flows))
INFO("[DONE] test_sync_freeze_and_edges")
get_agent().stop()
