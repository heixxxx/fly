"""rasgd 基础场景：T=3 全程求解。

断言：
  1. kickoff 非阻塞（快速返回 handle）
  2. 每步 sol_t 数值与 splu 精确解一致（rel_err < 1e-4）
  3. warm start：后续步迭代数 <= 首步（冷启动）
  4. worker 矩阵缓存复用：LDLT 冷构建恰 nsd 次（跨 T 步不重建）
  5. controller 数据流：b_2 保留（终止步）、b_0/b_1 被逐步清理
"""
import os

FLY_CASE_LOG_DIR = os.environ["FLY_CASE_LOG_DIR"]
import time
import shutil

import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

from _fly_log import INFO
from fly import open_db, get_config
from test import record
from fly.runtime import get_agent
from solver import (solve_ras_graph_dynamic, get_dynamic_result,
                    generate_poisson_matrix, MATRIX_OBJ_KEY, SolveDb)

N_SIDE = 20
NSD = 4
NUM_STEPS = 3

DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_basic")
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)

MATRIX_PATH = os.path.join(FLY_CASE_LOG_DIR, "matrix_basic.npz")
generate_poisson_matrix(N_SIDE, MATRIX_PATH)
_data = np.load(MATRIX_PATH, allow_pickle=False)
golden = {k: _data[k] for k in _data.files}

N = int(golden["N"])
A_sp = sparse.csc_matrix((golden["vals"], (golden["rows"], golden["cols"])),
                         shape=(N, N))
b0 = np.asarray(golden["b"], dtype=np.float64)

# 精确解对照（每时间步直接解）
lu = splu(A_sp)
x_exact = [lu.solve(b0 * (1.0 + 0.1 * t)) for t in range(NUM_STEPS)]


def update_rhs(x_prev, t):
    # EmIR 电流波形类比：确定性线性变化（验证用；真实场景 b_t = f(x_{t-1})）
    return b0 * (1.0 + 0.1 * t)


db = open_db(DB_PATH, db_cls=SolveDb)
db.write_object(MATRIX_OBJ_KEY, golden)

t0 = time.perf_counter()
handle = solve_ras_graph_dynamic(db, MATRIX_OBJ_KEY, NSD, b0, update_rhs,
                                 NUM_STEPS, overlap_ratio=0.30, max_iter=100,
                                 tol=1e-8, omega="coarse", min_steps=2)
kickoff_elapsed = time.perf_counter() - t0
INFO(f"kickoff returned in {kickoff_elapsed:.2f}s (non-blocking), handle={handle}")

# 无 timeout（裁定：数据规模相关等待不设超时；can_still_produce 兜底失败语义）
result = get_dynamic_result(db)
INFO(f"dynamic result: {result}")

# 原断言：assert result["num_steps_done"] == NUM_STEPS
record("num_steps_done", result["num_steps_done"])
# 原断言：assert len(result["iters"]) == NUM_STEPS
record("iters_count", len(result["iters"]) == NUM_STEPS,
       detail=list(result["iters"]))
# 保留：守卫——后面的 result["iters"][t]、result["iters"][0] 按步取下标
assert len(result["iters"]) == NUM_STEPS
# 原断言：assert all(result["converged"]), f"not all converged: {result['converged']}"
record("all_converged", all(result["converged"]), detail=list(result["converged"]))

for t in range(NUM_STEPS):
    x_t = np.asarray(db.read_object(f"__rasg__sol_{t}"))
    rel = np.linalg.norm(x_t - x_exact[t]) / np.linalg.norm(x_exact[t])
    INFO(f"t={t}: iters={result['iters'][t]} rel_err={rel:.2e}")
    # 原断言：assert rel < 1e-4, f"t={t} rel_err too large: {rel:.2e}"
    record(f"step{t}/rel_err_ok", rel < 1e-4, detail=f"{rel:.2e}")

for t in range(1, NUM_STEPS):
    # 原断言：assert result["iters"][t] <= result["iters"][0], \
    record(f"warm_start/step{t}_not_more_than_cold",
           result["iters"][t] <= result["iters"][0],
           detail=list(result["iters"]))
INFO(f"warm start OK: iters={result['iters']} (step 0 is the cold start)")



def _obj_exists(name):
    try:
        db.read_object(name)
        return True
    except Exception:
        return False


# 原断言：assert _obj_exists("__rasg__b_2"), "b_2 should exist (final step keeps its rhs)"
record("b_2_exists", _obj_exists("__rasg__b_2"), detail="__rasg__b_2")
# 原断言：assert not _obj_exists("__rasg__b_0"), "b_0 should be cleaned by controller(1)"
record("b_0_cleaned", not _obj_exists("__rasg__b_0"), detail="__rasg__b_0")
# 原断言：assert not _obj_exists("__rasg__b_1"), "b_1 should be cleaned by controller(2)"
record("b_1_cleaned", not _obj_exists("__rasg__b_1"), detail="__rasg__b_1")

get_agent().stop()
# worker 矩阵缓存复用：LDLT 冷构建恰 nsd 次（worker 日志在 FLY_CASE_SUB_DIR）
setup_count = 0
sub_dir = os.environ["FLY_CASE_SUB_DIR"]
for fn in sorted(os.listdir(sub_dir)):
    if fn.startswith("worker") and fn.endswith(".log"):
        with open(os.path.join(sub_dir, fn), errors="replace") as f:
            setup_count += f.read().count("LDLT done (cold)")
INFO(f"LDLT cold setups: {setup_count} (expect {NSD})")
# 原断言：assert setup_count == NSD, f"setup cache reuse broken: {setup_count} != {NSD}"
record("ldlt_setup_count_ok", setup_count == NSD, detail=setup_count)

INFO("[DONE] rasgd_basic")
