"""run1：Task db 归属规则验证——双 db 各自失败 task 分散落盘。

stage_a：自动推导归属（task 第一参数 db_a）→ 失败记录落 {stage_a}/failed_tasks.bin。
stage_b：显式 owner 覆盖（第一参数是上游 db_a，owner=lambda 指向 db_b）→
         失败记录落 {stage_b}/failed_tasks.bin（机制失效则会混入 stage_a）。

失败构造：unresolvable dep（fail_unscheduleable_tasks=1，确定性）。
"""
import os
import time

from _fly_log import INFO
from fly import as_task, get_config, open_project
from fly.runtime import get_agent

PROJ_PATH = os.environ["FLY_PROJ_PATH"]


def wait_for(condition, timeout=30.0, interval=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if condition():
            return True
        time.sleep(interval)
    return False


@as_task()
def ok_task(db, key, val):
    db.write_object(key, val)


@as_task(inputs=lambda db, key: [db.get_full_name("phantom_a")])
def dep_task(db, key):
    v = db.read_object("phantom_a")
    db.write_object(key, v)


@as_task(inputs=lambda db_up, db, key: [db_up.get_full_name("phantom_b")],
         owner=lambda db_up, db, key: db)
def solve_like_task(db_up, db, key):
    v = db_up.read_object("phantom_b")
    db.write_object(key, v)


get_config().set_int("fail_unscheduleable_tasks", 1)

master = get_agent()
master.launch_local_workers([{}, {}])
assert master.wait_for_workers(2)

proj = open_project(PROJ_PATH)
db_a = proj._create_db("stage_a")
db_b = proj._create_db("stage_b")

# stage_a：ok + 失败（自动归属 db_a）。
ok_task(db_a, "ok_a", 11)
dep_task(db_a, "dep_a")

# stage_b：ok + 失败（显式 owner=db_b，第一参数是上游 db_a）。
ok_task(db_b, "ok_b", 22)
solve_like_task(db_a, db_b, "cross")

assert wait_for(lambda: len(master.completed_tasks) >= 2), \
    f"expected 2 completed, got {len(master.completed_tasks)}"
assert wait_for(lambda: len(master.failed_tasks) >= 2), \
    f"expected 2 failed, got {len(master.failed_tasks)}"

bin_a = os.path.join(PROJ_PATH, "stage_a", "failed_tasks.bin")
bin_b = os.path.join(PROJ_PATH, "stage_b", "failed_tasks.bin")
assert os.path.isfile(bin_a), f"stage_a owner bin missing: {bin_a}"
assert os.path.isfile(bin_b), f"stage_b owner bin missing: {bin_b}"
assert os.path.getsize(bin_a) > 0 and os.path.getsize(bin_b) > 0, "bins must be non-empty"

INFO(f"[PASS] task_owner_run1: per-owner bins landed (a={bin_a}, b={bin_b})")
master.stop()
