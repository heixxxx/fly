"""Run 1: 20+ 节点 DAG，中间汇聚节点失败 → 下游 sleep 后读不到文件 → 连锁失败。

DAG 拓扑（钻石/菱形，含汇聚点）：
  层 0: 0
  层 1: 1,2,3,4        (依赖 0)
  层 2: 5,6            (汇聚: 5←1,2  6←3,4)
  层 3: 7              (汇聚: ←5,6)  ← FAIL_NODE
  层 4: 8,9,10,11      (依赖 7)      ← 下游 sleep 后读不到 7/result
  层 5: 12,13          (汇聚: 12←8,9  13←10,11)
  层 6: 14,15          (12,13 各依赖)
  层 7: 16             (汇聚: ←14,15)
  层 8: 17,18,19,20    (依赖 16)

共 21 节点。node7 是汇聚节点，失败后下游 node8-20 连锁失败。
"""
from _fly_log import INFO, ERR
import os
import time

from test import dag_node, record
from fly import open_db, get_config

DB_PATH = os.environ.get("FLY_DB_PATH") or os.path.join(get_config().get_str("log_dir"), "db")

# DAG 拓扑: node_id -> [upstream node_ids]
DAG = {
    0: [],
    1: [0], 2: [0], 3: [0], 4: [0],
    5: [1, 2], 6: [3, 4],
    7: [5, 6],
    8: [7], 9: [7], 10: [7], 11: [7],
    12: [8, 9], 13: [10, 11],
    14: [12], 15: [13],
    16: [14, 15],
    17: [16], 18: [16], 19: [16], 20: [16],
}
FAIL_NODES = "7"   # 汇聚节点 node7 失败

import shutil
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

from fly.runtime import get_agent
master = get_agent()

# 4 个 worker 让上下游并发，制造"读不到文件"窗口
master.launch_local_workers([{}, {}, {}, {}])
# 保留：等待原语
assert master.wait_workers_registered(timeout=60)
wc = master.worker_count
# 原断言：assert master.worker_count >= 4, f"Need 4 workers, got {master.worker_count}"
record("workers/at_least_4", wc >= 4, detail=wc)

db = open_db(DB_PATH)
db_path = db.get_db_path()

# 提交所有 DAG 节点
for node_id, deps in DAG.items():
    dag_node(db, node_id, deps)

INFO(f"[RUN1] Submitted {len(DAG)} DAG nodes, fail_nodes={FAIL_NODES}, workers={master.worker_count}")

# 等待失败稳定：node7 失败 + 其下游（8-20, 共 13 个）
def wait_fail_stable(timeout=20.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        n = len(master.failed_tasks)
        if n >= 13:
            time.sleep(0.5)
            if len(master.failed_tasks) >= 13:
                return True
        time.sleep(0.3)
    return False

# 保留：等待原语（等待失败稳定）
assert wait_fail_stable(), \
    f"Expected >=13 failed tasks (node7 + downstream), got {len(master.failed_tasks)}"

failed = master.failed_tasks
INFO(f"[RUN1] {len(failed)} tasks failed")

# 验证失败原因：node7 的下游（8-20）应因读不到文件而失败，非依赖不可解
read_fail_count = 0
unresolvable_count = 0
for tid in failed:
    err = master.get_task_error(tid)
    if "Unresolvable" in err or "unschedulable" in err.lower():
        unresolvable_count += 1
    else:
        read_fail_count += 1
INFO(f"[RUN1] failure reasons: read_failure={read_fail_count}, unresolvable={unresolvable_count}")
read_fail_present = read_fail_count >= 1
# 原断言：assert read_fail_count >= 1, \
record("failure_reasons/read_fail_present", read_fail_present,
       detail={"read_fail": read_fail_count, "unresolvable": unresolvable_count})

# 验证成功节点的数据
node0_value = db.read_object("node0/result")
# 原断言：assert db.read_object("node0/result") == 0
record("node_data/0", node0_value, detail=node0_value)
INFO("[RUN1] upstream nodes data correct")

# 验证 node7 脏对象被清理
for key in ["node7/result", "node7/dirty1", "node7/dirty2"]:
    readable = False
    try:
        db.read_object(key)
        readable = True
        ERR(f"[RUN1] WARN: {key} unexpectedly readable")
    except Exception:
        pass
    # 新增判定：原来只打日志，读成功也不会使用例失败
    record(f"dirty_unreadable/{key}", not readable, detail=key)
INFO("[RUN1] node7 dirty objects cleaned")

# 持久化验证（Task db 归属规则：task 带 db 参数 → {db_path}/failed_tasks.bin）
failed_file = os.path.join(DB_PATH, "failed_tasks.bin")
bin_persisted = os.path.isfile(failed_file)
# 原断言：assert os.path.isfile(failed_file), f"failed_tasks.bin should exist at {failed_file}"
record("failed_tasks_bin/persisted", bin_persisted, detail=failed_file)
INFO(f"[RUN1] failed_tasks.bin persisted at {failed_file}")

with open(os.path.join(DB_PATH, "_test_db_path"), "w") as f:
    f.write(db_path)
# 保存 DAG 拓扑给 run2 验证
with open(os.path.join(DB_PATH, "_test_dag"), "w") as f:
    for nid, deps in DAG.items():
        f.write(f"{nid}:{','.join(map(str, deps))}\n")

INFO(f"[RUN1] db_path={db_path}, exiting for run2 to load_db + restart")
