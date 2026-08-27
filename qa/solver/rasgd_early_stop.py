"""rasgd 提前终止场景：update_rhs 返回 None → 链在第 2 步后终止。

断言：num_steps_done == 2（t=0/1 完成），sol_2 不存在，sol_0/sol_1 数值正确。
"""
import os

FLY_CASE_LOG_DIR = os.environ["FLY_CASE_LOG_DIR"]
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
DB_PATH = os.path.join(FLY_CASE_LOG_DIR, "db_early")

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)

MATRIX_PATH = os.path.join(FLY_CASE_LOG_DIR, "matrix_early.npz")
generate_poisson_matrix(N_SIDE, MATRIX_PATH)
_data = np.load(MATRIX_PATH, allow_pickle=False)
golden = {k: _data[k] for k in _data.files}

N = int(golden["N"])
A_sp = sparse.csc_matrix((golden["vals"], (golden["rows"], golden["cols"])),
                         shape=(N, N))
b0 = np.asarray(golden["b"], dtype=np.float64)
lu = splu(A_sp)


def update_rhs(x_prev, t):
    if t >= 2:
        return None  # 提前终止：波形结束
    return b0 * (1.0 + 0.1 * t)


db = open_db(DB_PATH)
db.write_object(MATRIX_OBJ_KEY, golden)

solve_ras_graph_dynamic(db, MATRIX_OBJ_KEY, NSD, b0, update_rhs,
                        num_steps=5, overlap_ratio=0.30, max_iter=100,
                        tol=1e-8, omega="coarse", min_steps=2)

# 无 timeout 参数调用：覆盖 wait_obj 默认路径（曾因 inputs lambda 声明
# 必选 timeout 导致无参调用 TypeError 的回归锚点；timeout 裁定后此调用
# 形态即标准用法——数据规模相关等待不设超时，can_still_produce 兜底）。
result = get_dynamic_result(db)
INFO(f"dynamic result: {result}")

assert result["num_steps_done"] == 2, \
    f"early stop expected at 2 steps, got {result['num_steps_done']}"
assert len(result["sol_names"]) == 2
for t in range(2):
    x_t = np.asarray(db.read_object(f"__rasg__sol_{t}"))
    x_exact = lu.solve(b0 * (1.0 + 0.1 * t))
    rel = np.linalg.norm(x_t - x_exact) / np.linalg.norm(x_exact)
    assert rel < 1e-4, f"t={t} rel_err too large: {rel:.2e}"

try:
    db.read_object("__rasg__sol_2")
    raise AssertionError("sol_2 should not exist (stopped early)")
except AssertionError:
    raise
except Exception:
    pass  # 预期：不存在

get_agent().stop()
INFO("[PASS] rasgd_early_stop")
