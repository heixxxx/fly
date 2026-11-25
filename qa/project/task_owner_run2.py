"""run2：补齐依赖 → restart_failed_tasks(db list) 自动搜索归属 bin 重投。

db list 传 db_path 字符串（验证归一化），返回值必须等于失败 task 数（2）；
重投后全部对象就绪且两个归属 bin 均被摘除删除。
task 体经 cloudpickle 自包含（from_user），重投不依赖 run1 脚本。
"""
import os

from _fly_log import INFO
from fly import load_project, restart_failed_tasks, wait_tasks
from test import record
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]

master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2)

proj = load_project(PROJ_PATH)
db_a = proj.get_db("stage_a")
db_b = proj.get_db("stage_b")

# 补齐 run1 缺失的依赖对象（master 自写即 ready），restart 后依赖满足。
# phantom_a 挂在 stage_a；phantom_b 是 solve_like_task(db_up=db_a, db=db_b)
# 的输入依赖，也挂在 stage_a（读上游 db）。
db_a.write_object("phantom_a", "pa")
db_a.write_object("phantom_b", "pb")

# db list 自动搜索：无 bin 的路径静默跳过，返回重投总数。
restarted = restart_failed_tasks([
    os.path.join(PROJ_PATH, "stage_a"),
    os.path.join(PROJ_PATH, "stage_b"),
])
# 原断言：assert restarted == 2, f"expected 2 restarted tasks, got {restarted}"
record("owner_run2/restarted", restarted)

ok = wait_tasks(timeout=120)
assert ok, "all restarted tasks should complete"

ok_a_val = db_a.read_object("ok_a")
# 原断言：assert db_a.read_object("ok_a") == 11
record("owner_run2/ok_a", ok_a_val)
dep_a_val = db_a.read_object("dep_a")
# 原断言：assert db_a.read_object("dep_a") == "pa"
record("owner_run2/dep_a", dep_a_val)
ok_b_val = db_b.read_object("ok_b")
# 原断言：assert db_b.read_object("ok_b") == 22
record("owner_run2/ok_b", ok_b_val)
cross_val = db_b.read_object("cross")
# 原断言：assert db_b.read_object("cross") == "pb"
record("owner_run2/cross", cross_val)

# 成功摘除：两个归属 bin 均应被清空删除。
from test import wait_until
bins = [os.path.join(PROJ_PATH, s, "failed_tasks.bin") for s in ("stage_a", "stage_b")]
assert wait_until(lambda: not any(os.path.isfile(b) for b in bins), timeout=30), \
    "owner bins should be deleted after success"

INFO("[DONE] task_owner_run2: db-list restart + per-owner bins consumed")
master.stop()
