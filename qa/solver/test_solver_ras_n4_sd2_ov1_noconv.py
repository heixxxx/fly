"""小规模快速收敛回归：n=4, subdomains=2, overlap=1。Workers=2.

历史：ras.py GMRES-RAS restart 截断语义测试（docstring 曾表述"不收敛"，
实际断言一直是收敛——迁移 dynamic 时修正表述）。现语义：小规模经
dynamic 单步求解必须收敛。
"""
from _fly_log import INFO
from test import qa_tmp
import os
import shutil
import numpy as np


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_once, MATRIX_OBJ_KEY, SolveDb

N = 4
NSD = 2
OVERLAP = 1
DB_PATH = qa_tmp(f"fly_e2e_solver_ras_n4_sd2_ov1_noconv_db_{os.getpid()}")

if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)
master = get_agent()
master.launch_local_workers([{} for _ in range(NSD)])
assert master.wait_for_workers(NSD), "workers should connect"

db = open_db(DB_PATH, db_cls=SolveDb)

# 五点模板矩阵入库 + dynamic 单步（求解器收敛迁移，2026-08-31）
_rows, _cols, _vals = [], [], []
for i in range(N):
    for j in range(N):
        k = i * N + j
        _rows.append(k); _cols.append(k); _vals.append(4.0)
        if i > 0: _rows.append(k); _cols.append((i - 1) * N + j); _vals.append(-1.0)
        if i < N - 1: _rows.append(k); _cols.append((i + 1) * N + j); _vals.append(-1.0)
        if j > 0: _rows.append(k); _cols.append(i * N + (j - 1)); _vals.append(-1.0)
        if j < N - 1: _rows.append(k); _cols.append(i * N + (j + 1)); _vals.append(-1.0)
db.write_object(MATRIX_OBJ_KEY, {
    "n": N, "N": N * N,
    "rows": np.array(_rows, dtype=np.int64),
    "cols": np.array(_cols, dtype=np.int64),
    "vals": np.array(_vals, dtype=np.float64),
    "b": np.ones(N * N, dtype=np.float64),
})

result = solve_once(db, MATRIX_OBJ_KEY, NSD,
                    overlap_ratio=OVERLAP / N, max_iter=100, tol=1e-8)

INFO(f"  iters={result['iters']}, converged={result['converged']}")
assert result["converged"], \
    f"small case should converge for n={N} nsd={NSD} ov={OVERLAP}"

master.stop()
INFO("[PASS] test_solver_ras_n4_sd2_ov1_noconv")
