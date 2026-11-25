"""Run 2: load_db 恢复数据 + restart_failed_tasks → 全部成功。

restart 后 FLY_FAIL_NODES 未设（不失败），所有节点重跑成功。
验证：DAG 所有节点数据正确、无脏残留、无重复写/拒绝。
"""
from _fly_log import INFO, ERR
import os
import time

from test import record
from fly import get_config

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

# 编排脚本挪入的检查（原先在两个 sub case 之间内联断言，重录模式会真的执行
# 内联语句而失败，见 qa/README.md「编排脚本的写法约定」）：时机不变——run1
# 的工作刚结束、本 sub case 开始工作之前。
# 保留：守卫——后续 open 该 marker 文件还要用
assert os.path.isfile(os.path.join(DB_PATH, "_test_db_path")), "marker should exist after run1"

# 读取 DAG 拓扑
DAG = {}
with open(os.path.join(DB_PATH, "_test_dag")) as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        nid_str, deps_str = line.split(":", 1)
        nid = int(nid_str)
        deps = [int(x) for x in deps_str.split(",") if x] if deps_str else []
        DAG[nid] = deps

with open(os.path.join(DB_PATH, "_test_db_path")) as f:
    expected_db_path = f.read().strip()

from fly.runtime import get_agent
master = get_agent()

db = master.load_db(DB_PATH)
INFO(f"[RUN2] load_db: {db}")

# 保留：等待原语
assert master.wait_workers_registered(timeout=60)
wc = master.worker_count
# 原断言：assert master.worker_count >= 1, "Worker should connect after load_db"
record("workers/at_least_1", wc >= 1, detail=wc)
time.sleep(1.0)

db_path_ok = db.get_db_path() == expected_db_path
# 原断言：assert db.get_db_path() == expected_db_path, \
record("load_db/path_preserved", db_path_ok,
       detail={"actual": db.get_db_path(), "expected": expected_db_path})

# restart failed tasks：读 run1 落在归属 db 目录的 failed_tasks.bin
# （Task db 归属规则：task 带 db 参数 → {db_path}/failed_tasks.bin，run1/run2 共享 DB_PATH）。
failed_file = os.path.join(DB_PATH, "failed_tasks.bin")
bin_persisted = os.path.isfile(failed_file)
# 原断言：assert os.path.isfile(failed_file), f"failed_tasks.bin should exist: {failed_file}"
record("failed_tasks_bin/persisted", bin_persisted, detail=failed_file)

master.restart_failed_tasks(db)
INFO("[RUN2] restart_failed_tasks called")

def wait_all_done(timeout=20.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        failed = master.failed_tasks
        if failed:
            ERR(f"[RUN2] unexpected failure after restart: {failed}")
            for tid in failed:
                ERR(f"[RUN2]   task {tid}: {master.get_task_error(tid)[:150]}")
            return False
        if not master.pending_tasks and not master.running_tasks:
            return True
        time.sleep(0.3)
    return False

# 保留：等待原语（等待全部重启任务结束）
assert wait_all_done(), \
    f"Not all done: pending={master.pending_tasks}, running={master.running_tasks}, failed={master.failed_tasks}"

INFO("[RUN2] all restarted tasks completed successfully")

# 验证全 DAG 数据正确：nodeN/result = sum(upstream results) + N
for node_id in sorted(DAG.keys()):
    deps = DAG[node_id]
    expected = sum(db.read_object(f"node{d}/result") for d in deps) + node_id
    val = db.read_object(f"node{node_id}/result")
    # 原断言：assert val == expected, f"node{node_id}/result mismatch: {val} != {expected}"
    # 条件记录：DAG 从 run1 写出的拓扑文件加载，节点集合由 run1 的脚本确定；通过路径上这个循环必然完整执行
    record(f"node_data/{node_id}", val == expected,
           detail={"val": val, "expected": expected})

INFO("[RUN2] all DAG data correct")

# 验证失败节点的脏对象不存在
for key in ["node7/dirty1", "node7/dirty2"]:
    readable = False
    try:
        db.read_object(key)
        readable = True
        ERR(f"[RUN2] WARN: {key} should not exist")
    except Exception:
        pass
    # 新增判定：原来只打日志，读成功也不会使用例失败
    record(f"dirty_unreadable/{key}", not readable, detail=key)
INFO("[RUN2] no dirty object residue")

INFO("[RUN2] DONE: DAG restart completed, all data correct, no residue")
