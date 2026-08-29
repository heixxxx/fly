"""Verify 2D partition + adaptive depth convergence."""
from _fly_log import INFO
from test import qa_tmp
import sys
import os
import shutil
import math
import time
import numpy as np
from scipy import sparse


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph, SolveDb

get_config().set_int("fail_unscheduleable_tasks", 1)


def build_poisson_2d(n):
    N = n * n
    diags = [4.0 * np.ones(N),
             -1.0 * np.ones(N - 1), -1.0 * np.ones(N - 1),
             -1.0 * np.ones(N - n), -1.0 * np.ones(N - n)]
    A = sparse.diags(diags, [0, 1, -1, n, -n], shape=(N, N), format='lil')
    for i in range(1, n):
        A[i * n - 1, i * n] = 0.0
        A[i * n, i * n - 1] = 0.0
    A_csc = A.tocsc()
    rows, cols, vals = [], [], []
    for k in range(A_csc.shape[1]):
        start, end = A_csc.indptr[k], A_csc.indptr[k + 1]
        for p in range(start, end):
            rows.append(int(A_csc.indices[p]))
            cols.append(int(k))
            vals.append(float(A_csc.data[p]))
    return N, rows, cols, vals


N_SIDE = int(os.environ.get("BN", "100"))
NSD = int(os.environ.get("BNSD", "4"))
MAX_ITER = 100

N, rows, cols, vals = build_poisson_2d(N_SIDE)
b = [1.0] * N

db_path = qa_tmp(f"fly_verify_2d_n{N_SIDE}_nsd{NSD}")
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
# ensure 的静态预检按「当前在册池」判定——连接数够不等于注册完成，等 IDLE 口径。
master.wait_for_all_workers(NSD, timeout=60)

db = open_db(db_path, db_cls=SolveDb)

t0 = time.perf_counter()
sol = solve_ras_graph(db, N, rows, cols, vals, b, NSD,
                      overlap_ratio=0.30, max_iter=MAX_ITER, tol=1e-8)
elapsed = time.perf_counter() - t0

INFO(f"n={N_SIDE} nsd={NSD} iters={sol['iters']} conv={sol['converged']} "
     f"time={elapsed:.2f}s")

master.stop()

if not sol["converged"]:
    INFO(f"FAILED: did not converge in {MAX_ITER} iterations")
    sys.exit(1)

INFO(f"[PASS] verify_2d_partition n={N_SIDE} nsd={NSD}")
