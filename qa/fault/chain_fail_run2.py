"""Run 2: load_db 恢复数据 + restart_failed_tasks → 全部成功。

restart 后 FLY_FAIL_NODES 未设（不失败），所有节点重跑成功。
验证：DAG 所有节点数据正确、无脏残留、无重复写/拒绝。
"""
from _fly_log import INFO, ERR
import os
import time

from fly import get_config

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

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

for _ in range(40):
    if master.worker_count >= 1:
        break
    time.sleep(0.5)
assert master.worker_count >= 1, "Worker should connect after load_db"
time.sleep(1.0)

assert db.get_db_path() == expected_db_path, \
    f"db_path mismatch: {db.get_db_path()} != {expected_db_path}"

# restart failed tasks：读 run1 的 failed_tasks.bin。
# .pyt 模式经 env FLY_RUN1_LOG_DIR 传 run1 的 log_dir；旧 subprocess wrapper 兼容 fallback。
run1_log = os.environ.get("FLY_RUN1_LOG_DIR")
if run1_log:
    failed_file = os.path.join(run1_log, "failed_tasks.bin")
else:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    failed_file = os.path.join(SCRIPT_DIR, "test_chain_failure_restart.1", "failed_tasks.bin")
assert os.path.isfile(failed_file), f"failed_tasks.bin should exist: {failed_file}"

master.restart_failed_tasks(failed_file)
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

assert wait_all_done(), \
    f"Not all done: pending={master.pending_tasks}, running={master.running_tasks}, failed={master.failed_tasks}"

INFO("[RUN2] all restarted tasks completed successfully")

# 验证全 DAG 数据正确：nodeN/result = sum(upstream results) + N
for node_id in sorted(DAG.keys()):
    deps = DAG[node_id]
    expected = sum(db.read_object(f"node{d}/result") for d in deps) + node_id
    val = db.read_object(f"node{node_id}/result")
    assert val == expected, f"node{node_id}/result mismatch: {val} != {expected}"

INFO("[RUN2] all DAG data correct")

# 验证失败节点的脏对象不存在
for key in ["node7/dirty1", "node7/dirty2"]:
    try:
        db.read_object(key)
        ERR(f"[RUN2] WARN: {key} should not exist")
    except Exception:
        pass
INFO("[RUN2] no dirty object residue")

INFO("[RUN2] PASS: DAG restart completed, all data correct, no residue")
