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
from fly.runtime import get_agent
from solver import (solve_ras_graph_dynamic, get_dynamic_result,
                    generate_poisson_matrix, MATRIX_OBJ_KEY)

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


db = open_db(DB_PATH)
db.write_object(MATRIX_OBJ_KEY, golden)

t0 = time.perf_counter()
handle = solve_ras_graph_dynamic(db, MATRIX_OBJ_KEY, NSD, b0, update_rhs,
                                 NUM_STEPS, overlap_ratio=0.30, max_iter=100,
                                 tol=1e-8, omega="coarse", min_steps=2)
kickoff_elapsed = time.perf_counter() - t0
INFO(f"kickoff returned in {kickoff_elapsed:.2f}s (non-blocking), handle={handle}")

result = get_dynamic_result(db, timeout=180)
INFO(f"dynamic result: {result}")

assert result["num_steps_done"] == NUM_STEPS
assert len(result["iters"]) == NUM_STEPS
assert all(result["converged"]), f"not all converged: {result['converged']}"

for t in range(NUM_STEPS):
    x_t = np.asarray(db.read_object(f"__rasg__sol_{t}"))
    rel = np.linalg.norm(x_t - x_exact[t]) / np.linalg.norm(x_exact[t])
    INFO(f"t={t}: iters={result['iters'][t]} rel_err={rel:.2e}")
    assert rel < 1e-4, f"t={t} rel_err too large: {rel:.2e}"

for t in range(1, NUM_STEPS):
    assert result["iters"][t] <= result["iters"][0], \
        f"warm start ineffective: iters={result['iters']}"
INFO(f"warm start OK: iters={result['iters']} (step 0 is the cold start)")



def _obj_exists(name):
    try:
        db.read_object(name)
        return True
    except Exception:
        return False


assert _obj_exists("__rasg__b_2"), "b_2 should exist (final step keeps its rhs)"
assert not _obj_exists("__rasg__b_0"), "b_0 should be cleaned by controller(1)"
assert not _obj_exists("__rasg__b_1"), "b_1 should be cleaned by controller(2)"

get_agent().stop()
# worker 矩阵缓存复用：LDLT 冷构建恰 nsd 次（worker 日志在 FLY_CASE_SUB_DIR）
setup_count = 0
sub_dir = os.environ["FLY_CASE_SUB_DIR"]
for fn in sorted(os.listdir(sub_dir)):
    if fn.startswith("worker") and fn.endswith(".log"):
        with open(os.path.join(sub_dir, fn), errors="replace") as f:
            setup_count += f.read().count("LDLT setup done (cold)")
INFO(f"LDLT cold setups: {setup_count} (expect {NSD})")
assert setup_count == NSD, f"setup cache reuse broken: {setup_count} != {NSD}"

INFO("[PASS] rasgd_basic")
