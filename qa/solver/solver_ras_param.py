"""RAS solver 参数化收敛测试（经 env SOLVER_N/SOLVER_NSD/SOLVER_OVERLAP 注入参数）。

2026-08-16 收敛：替代 13 个逐文件复制的 test_solver_ras_n*_sd*_ov*.py
（每文件仅 3 行参数不同），由 test_solver_ras_matrix.pyt 循环编排。
非收敛语义（maxiter 截断）独立于本矩阵，见 test_solver_ras_n4_sd2_ov1_noconv。
"""
from _fly_log import INFO
import os
import shutil
import time
import numpy as np


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras

N = int(os.environ.get("SOLVER_N", "4"))
NSD = int(os.environ.get("SOLVER_NSD", "2"))
OVERLAP = int(os.environ.get("SOLVER_OVERLAP", "1"))
DB_PATH = f"/tmp/fly_e2e_solver_ras_n{N}_sd{NSD}_ov{OVERLAP}_db_{os.getpid()}"


def scipy_reference(n):
    import scipy.sparse as sp
    import scipy.sparse.linalg as spla
    N = n * n
    rows, cols, vals = [], [], []
    for i in range(n):
        for j in range(n):
            k = i * n + j
            rows.append(k); cols.append(k); vals.append(4.0)
            if i > 0: rows.append(k); cols.append((i - 1) * n + j); vals.append(-1.0)
            if i < n - 1: rows.append(k); cols.append((i + 1) * n + j); vals.append(-1.0)
            if j > 0: rows.append(k); cols.append(i * n + (j - 1)); vals.append(-1.0)
            if j < n - 1: rows.append(k); cols.append(i * n + (j + 1)); vals.append(-1.0)
    A = sp.csr_matrix((vals, (rows, cols)), shape=(N, N))
    return spla.spsolve(A, np.ones(N))


if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)
master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), "workers should connect"

db = open_db(DB_PATH)
x_ref = scipy_reference(N)

t0 = time.time()
result = solve_ras(db, N, NSD, OVERLAP)
elapsed = time.time() - t0

x_ras = np.array(result["x"])
error = np.linalg.norm(x_ras - x_ref) / np.linalg.norm(x_ref)

assert result["converged"], \
    f"Did not converge: iters={result['iters']}, res={result['residual']:.2e}"
assert error < 1e-2, f"Error too large: {error:.2e}"

INFO(f"OK n={N} sd={NSD} ov={OVERLAP} iters={result['iters']:3d} res={result['residual']:.2e} "
      f"err={error:.2e} time={elapsed:.2f}s workers={NSD}")

master.stop()
INFO(f"[PASS] solver_ras n={N} sd={NSD} ov={OVERLAP}")
