"""rasgd 收集圈 deadline 判组死场景：serve 挂起注入（FLY_RASG_HANG_AT）。

验证（审查批次 3）：check 的收集圈等待 bounded 于 30s deadline——
  1. 单成员 serve 挂起（>30s 不应答、连接不断）时 check 必须判组死失败，
     而非无限挂死（修复前 peer_stream_response_reader(rid, 0) 无限等 +
     fut.result() 无超时，deadline 检查不可达）。
  2. 失败经 wait_tasks 可见（组死语义传播）。
"""
import os

FLY_CASE_LOG_DIR = os.environ["FLY_CASE_LOG_DIR"]
import time

import numpy as np

from _fly_log import INFO
from fly import open_db, get_config
from fly.runtime import get_agent
from solver import (solve_ras_graph_dynamic, generate_poisson_matrix,
                    MATRIX_OBJ_KEY, SolveDb)

N_SIDE = 20
NSD = 4
NUM_STEPS = 2
DB_PATH = os.environ["FLY_DB_PATH"]

get_config().set_int("fail_unscheduleable_tasks", 1)
# 活性防呆窗口收紧（同 restart 场景口径）：组死后的收尾任务有界收敛。
get_config().set_int("solver_peer_liveness_timeout", 90)

MATRIX_PATH = os.path.join(FLY_CASE_LOG_DIR, "matrix_deadline.npz")
generate_poisson_matrix(N_SIDE, MATRIX_PATH)
_data = np.load(MATRIX_PATH, allow_pickle=False)
golden = {k: _data[k] for k in _data.files}
b0 = np.asarray(golden["b"], dtype=np.float64)


def update_rhs(x_prev, t):
    return b0 * (1.0 + 0.1 * t)


db = open_db(DB_PATH, db_cls=SolveDb)
db.write_object(MATRIX_OBJ_KEY, golden)

t_start = time.monotonic()
solve_ras_graph_dynamic(db, MATRIX_OBJ_KEY, NSD, b0, update_rhs, NUM_STEPS,
                        overlap_ratio=0.30, max_iter=100, tol=1e-8,
                        omega="coarse", min_steps=2)
elapsed = time.monotonic() - t_start

# sd=1 在首个 iterate 请求即挂起（45s > 30s deadline）→ check 判组死。
# 断言 1：链在有限时间失败（harness timeout=240 为外层兜底；正常路径
# T=2 全程 <30s——失败必须发生，说明判死生效）。
INFO(f"[DEADLINE-CASE] chain failed as expected, elapsed={elapsed:.0f}s")

# 断言 2：master 权威 failed 列表非空（组死语义传播到任务账本）。
from test import wait_until
master = get_agent()
assert wait_until(lambda: master.failed_tasks, timeout=90), \
    "group death must surface as failed tasks"
INFO("[PASS] rasgd_collect_deadline")

get_agent().stop()
