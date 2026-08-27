"""rasgd restart 场景 run1：失败注入（compute(2, sd=1) raise）中途失败。

验证：
  1. 组失败原子传染（wait_tasks 捕获失败，不挂死）
  2. 已完成的结果不丢：sol_0/sol_1 在库（哈希存盘供 run2 对比）
  3. failed_tasks.bin 落盘（restart 重投依据）
"""
import os

FLY_CASE_LOG_DIR = os.environ["FLY_CASE_LOG_DIR"]
import shutil
import hashlib
import json
import time

import numpy as np

from _fly_log import INFO, WARN
from fly import open_db, get_config
from fly.runtime import get_agent
from solver import (solve_ras_graph_dynamic, generate_poisson_matrix,
                    MATRIX_OBJ_KEY, SolveDb)

N_SIDE = 20
NSD = 4
NUM_STEPS = 3
DB_PATH = os.environ["FLY_DB_PATH"]

get_config().set_int("fail_unscheduleable_tasks", 1)

# 注入场景的活性防呆窗口收紧到 90s：组死后的迟到成员（并行调度下晚于
# check 开始执行的 compute）在窗口内退出，测试有界收敛；生产默认
# 1800s 由 config 键 solver_peer_liveness_timeout 全局控制。
get_config().set_int("solver_peer_liveness_timeout", 90)

# DB 由 .pyt setup 清理，这里不删（run2 要接着用）

MATRIX_PATH = os.path.join(FLY_CASE_LOG_DIR, "matrix_restart.npz")
generate_poisson_matrix(N_SIDE, MATRIX_PATH)
_data = np.load(MATRIX_PATH, allow_pickle=False)
golden = {k: _data[k] for k in _data.files}
b0 = np.asarray(golden["b"], dtype=np.float64)


def update_rhs(x_prev, t):
    return b0 * (1.0 + 0.1 * t)


db = open_db(DB_PATH, db_cls=SolveDb)
db.write_object(MATRIX_OBJ_KEY, golden)

solve_ras_graph_dynamic(db, MATRIX_OBJ_KEY, NSD, b0, update_rhs, NUM_STEPS,
                        overlap_ratio=0.30, max_iter=100, tol=1e-8,
                        omega="coarse", min_steps=2)

# 链在 t=2 组失败（FLY_RASG_FAIL_AT=2:1 → 组传染）。以 master 权威
# failed 列表轮询（0.5s 采样瞬时快照，无窗口语义；卡死由 harness 兜底）。
# 不用 wait_tasks：其"队列空即返回"与组失败的落账存在竞速窗口。
master = get_agent()
deadline_cycles = 0
while not master.failed_tasks:
    time.sleep(0.5)
    deadline_cycles += 1
    assert deadline_cycles < 240, "chain did not fail within harness budget"
WARN(f"expected failure observed: {master.failed_tasks}")

# 已完成结果不丢
for t in range(2):
    x_t = np.asarray(db.read_object(f"__rasg__sol_{t}"))
    INFO(f"sol_{t} preserved, md5={hashlib.md5(x_t.tobytes()).hexdigest()[:12]}")

digests = {f"__rasg__sol_{t}": hashlib.md5(np.asarray(db.read_object(f"__rasg__sol_{t}")).tobytes()).hexdigest()
           for t in range(2)}
with open(os.path.join(FLY_CASE_LOG_DIR, "restart_digests.json"), "w") as f:
    json.dump(digests, f)

bin_path = os.path.join(DB_PATH, "failed_tasks.bin")
assert os.path.isfile(bin_path), f"failed_tasks.bin missing at {bin_path}"

get_agent().stop()
INFO("[PASS] rasgd_restart_run1 (failed mid-run, results preserved)")
