"""E2E: ensure_workers 双求解 flow 并发防碰撞（issue 009 收紧语义）。

Worker: 6 个空属性 hybrid。两个 SolveDb（不同 uid = 不同属性命名空间）。
覆盖：
  1. 属性经 SolveDb.worker_attr 生成：rasg:{uid}:{tag}，两 flow uid 不同
     → 属性字符串零交集；
  2. flow2 ensure 时 exclude=r"^rasg:" 排除已被 flow1 编队的 worker →
     两支编队物理不重叠（属性→worker 集合的交集为空）；
  3. 并发提交两个 flow 各自 requires 的 task，都成功完成——调度按完整
     字符串匹配精确命中本 flow 编队，不串池。

这正是 exclude 参数的设计意图：并发 flow 分配到同一 worker 会产生碰撞。
"""
import os
import shutil
import time

from _fly_log import INFO

from fly import as_task, ensure_workers, open_db, get_work_directory
from fly.runtime import get_agent
from solver import SolveDb
from test import record

N_WORKERS = 6


def _db_path(tag):
    return os.path.join(get_work_directory(), f"db_{tag}")


def cleanup():
    for tag in ("f1", "f2"):
        p = _db_path(tag)
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)


def caps_by_worker(master):
    result = {}
    agent = master._agent
    for wid in list(agent.get_idle_workers()) + list(agent.get_busy_workers()):
        if wid not in result:
            result[wid] = set(agent.get_worker_capabilities(wid))
    return result


master = get_agent()
master.launch_local_workers([{} for _ in range(N_WORKERS)])
assert master.wait_for_workers(N_WORKERS, timeout=30), "workers failed to connect"

cleanup()
db1 = open_db(_db_path("f1"), db_cls=SolveDb)
db2 = open_db(_db_path("f2"), db_cls=SolveDb)

# 不同 db 实例 uid 必不同——属性命名空间隔离的前提
uid1 = db1.get_uid()
uid2 = db2.get_uid()
uids_valid = bool(uid1) and bool(uid2) and uid1 != uid2
# 原断言：assert db1.get_uid() and db2.get_uid() and db1.get_uid() != db2.get_uid()
record("flows/uid_distinct", uids_valid,
       detail={"uid1_nonempty": bool(uid1), "uid2_nonempty": bool(uid2),
               "distinct": uid1 != uid2})

req1 = [db1.worker_attr("sd_0"), db1.worker_attr("check")]
req2 = [db2.worker_attr("sd_0"), db2.worker_attr("check")]
all_rasg = all(a.startswith("rasg:") for a in req1 + req2)
# 原断言：assert all(a.startswith("rasg:") for a in req1 + req2)
record("flows/attrs_rasg_prefixed", all_rasg, detail=req1 + req2)
no_overlap = not (set(req1) & set(req2))
# 原断言：assert not (set(req1) & set(req2)), "uid 命名空间保证两 flow 属性零交集"
record("flows/attrs_no_overlap", no_overlap,
       detail=sorted(set(req1) & set(req2)))

t0 = time.time()
ensure_workers(req1, timeout=10.0, exclude=r"^rasg:")
ensure_workers(req2, timeout=10.0, exclude=r"^rasg:")
INFO(f"[DONE] two flows ensured in {time.time() - t0:.2f}s")

caps = caps_by_worker(master)
h1_a = {w for w, c in caps.items() if req1[0] in c}
h1_c = {w for w, c in caps.items() if req1[1] in c}
h2_a = {w for w, c in caps.items() if req2[0] in c}
h2_c = {w for w, c in caps.items() if req2[1] in c}
spec_counts_ok = len(h1_a) == len(h1_c) == len(h2_a) == len(h2_c) == 1
# 原断言：assert len(h1_a) == len(h1_c) == len(h2_a) == len(h2_c) == 1, f"each spec on one worker: {caps}"
record("flows/each_spec_on_one_worker", spec_counts_ok,
       detail={str(k): sorted(v) for k, v in caps.items()})
flow1_holders = h1_a | h1_c
flow2_holders = h2_a | h2_c
fleets_disjoint = not (flow1_holders & flow2_holders)
# 原断言：assert not (flow1_holders & flow2_holders), \
record("flows/fleets_disjoint", fleets_disjoint,
       detail={"flow1": sorted(flow1_holders), "flow2": sorted(flow2_holders)})
branded = flow1_holders | flow2_holders
unbranded = {w for w, c in caps.items()
             if not any(a.startswith("rasg:") for a in c)}
fleet_size_ok = len(branded) == 4 and len(unbranded) == N_WORKERS - 4
# 原断言：assert len(branded) == 4 and len(unbranded) == N_WORKERS - 4, \
record("flows/fleet_size_exact", fleet_size_ok,
       detail={"branded": sorted(branded), "unbranded": sorted(unbranded)})


@as_task(requires=[db1.worker_attr("sd_0")])
def flow1_work(db):
    db.write_object("done_f1", "1")


@as_task(requires=[db2.worker_attr("sd_0")])
def flow2_work(db):
    db.write_object("done_f2", "2")


flow1_work(db1)
flow2_work(db2)
deadline = time.time() + 30
while len(master.completed_tasks) < 2:
    # 保留：等待循环体内
    assert time.time() < deadline, \
        f"both flows' tasks must complete: failed={master.failed_tasks}"
    # 保留：等待循环体内
    assert not master.failed_tasks, \
        f"unexpected failures: {master.get_task_error(master.failed_tasks[0])}"
    time.sleep(0.2)
INFO("[DONE] concurrent flows scheduled precisely onto own fleet")

master.stop()
INFO("[DONE] ensure_workers dual-flow isolation")
