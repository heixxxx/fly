"""RAS 求解器参数化收敛用例的公共实现。

用法：每个入口脚本设置一组参数，然后调用——
    from solver_ras_case import run_solver_ras_case
    run_solver_ras_case(4, 2, 1)

13 个入口脚本（test_solver_ras_n*_sd*_ov*.py）由 test_solver_ras_matrix.pyt 逐个
调度，每组参数各占一个 fly 进程。记录按调用方（入口脚本名）落盘，所以每组参数
各有一份基准。
非收敛语义（maxiter 截断）独立于本矩阵，见 test_solver_ras_n4_sd2_ov1_noconv。
"""
from _fly_log import INFO
from test import qa_tmp, record
import os
import shutil
import time
import numpy as np


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_once, MATRIX_OBJ_KEY, SolveDb


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


def run_solver_ras_case(N, NSD, OVERLAP):
    """建泊松矩阵 → NSD 个子域的分布式 RAS 求解 → 与 scipy 参考解比较。

    N 是问题规模（矩阵为 N*N 阶），NSD 是子域数（也是 worker 数），OVERLAP 是
    相邻子域的重叠行数。
    """
    db_path = qa_tmp(f"fly_e2e_solver_ras_n{N}_sd{NSD}_ov{OVERLAP}_db_{os.getpid()}")
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    get_config().set_int("fail_unscheduleable_tasks", 1)
    master = get_agent()
    master.launch_local_workers([{} for _ in range(NSD)])
    assert master.wait_for_workers(NSD), "workers should connect"

    db = open_db(db_path, db_cls=SolveDb)
    x_ref = scipy_reference(N)

    # 求解器收敛（2026-08-31）：ras.py 朴素入口退役——矩阵入库 + dynamic 单步。
    rows, cols, vals = [], [], []
    for i in range(N):
        for j in range(N):
            k = i * N + j
            rows.append(k); cols.append(k); vals.append(4.0)
            if i > 0: rows.append(k); cols.append((i - 1) * N + j); vals.append(-1.0)
            if i < N - 1: rows.append(k); cols.append((i + 1) * N + j); vals.append(-1.0)
            if j > 0: rows.append(k); cols.append(i * N + (j - 1)); vals.append(-1.0)
            if j < N - 1: rows.append(k); cols.append(i * N + (j + 1)); vals.append(-1.0)
    db.write_object(MATRIX_OBJ_KEY, {
        "n": N, "N": N * N,
        "rows": np.array(rows, dtype=np.int64),
        "cols": np.array(cols, dtype=np.int64),
        "vals": np.array(vals, dtype=np.float64),
        "b": np.ones(N * N, dtype=np.float64),
    })

    t0 = time.time()
    result = solve_once(db, MATRIX_OBJ_KEY, NSD,
                        overlap_ratio=OVERLAP / N if N else 0.5, max_iter=100,
                        tol=1e-8)
    elapsed = time.time() - t0

    x_ras = np.array(result["x"])
    error = np.linalg.norm(x_ras - x_ref) / np.linalg.norm(x_ref)

    # 原断言：assert result["converged"], \
    record("converged", result["converged"], detail={"iters": int(result["iters"])})
    # 带容差的数值比较只记判定；误差是浮点数，用格式化字符串放进诊断信息，不进基准
    # 原断言：assert error < 1e-2, f"Error too large: {error:.2e}"
    record("error_within_tol", error < 1e-2, detail=f"{error:.2e}")

    INFO(f"OK n={N} sd={NSD} ov={OVERLAP} iters={result['iters']:3d} "
         f"err={error:.2e} time={elapsed:.2f}s workers={NSD}")

    master.stop()
    INFO(f"[DONE] solver_ras n={N} sd={NSD} ov={OVERLAP}")
