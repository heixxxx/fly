"""Benchmark: ras.py (centralized) vs ras_graph.py (distributed, graph overlap).

Usage:
  ./fly.sh build //src/main/cpp:fly && ./fly.sh install
  ./build/bin/fly qa/bench_ras_graph.py
"""
from _fly_log import INFO
from test import qa_tmp
import os
import shutil
import time
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras, solve_ras_graph

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


def build_chip_matrix(n):
    """Build chip resistance matrix with non-local connections."""
    N = n * n
    rows, cols, vals = [], [], []
    for i in range(n):
        for j in range(n):
            k = i * n + j
            rows.append(k); cols.append(k); vals.append(4.0)
            if j < n - 1:
                rows.append(k); cols.append(k + 1); vals.append(-1.0)
            if j > 0:
                rows.append(k); cols.append(k - 1); vals.append(-1.0)
            if i < n - 1:
                rows.append(k); cols.append((i + 1) * n + j); vals.append(-1.0)
            if i > 0:
                rows.append(k); cols.append((i - 1) * n + j); vals.append(-1.0)
            # Non-local connections (chip power grid pattern)
            if i == 0 and j < n - 1:
                rows.append(k); cols.append((n - 1) * n + j + 1); vals.append(-0.5)
    return N, rows, cols, vals


# ── Config ──
N_SIDE = int(os.environ.get("BN", "20"))
NSD = int(os.environ.get("BNSD", "4"))
DEPTH = int(os.environ.get("BDEPTH", "2"))
MATRIX = os.environ.get("BMATRIX", "poisson")  # poisson or chip

INFO(f"=== Benchmark: matrix={MATRIX} n={N_SIDE} nsd={NSD} depth={DEPTH} ===")

# ── Build matrix ──
if MATRIX == "chip":
    N, rows, cols, vals = build_chip_matrix(N_SIDE)
else:
    N, rows, cols, vals = build_poisson_2d(N_SIDE)

b = [1.0] * N

# ── Exact solution ──
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N, N))
x_exact = splu(A_sp).solve(np.array(b))

# ── Start workers with sd_N attributes (needed by solve_ras baseline) ──
master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), "workers should connect"

# ── Run RAS (baseline, geometric overlap) ──
db_path = qa_tmp(f"fly_bench_ras_{MATRIX}_n{N_SIDE}")
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)
db = open_db(db_path)

t0 = time.perf_counter()
result_ras = solve_ras(db, n=N_SIDE, num_subdomains=NSD)
t_ras = time.perf_counter() - t0

x_ras = np.array(result_ras["x"])
err_ras = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
INFO(f"RAS:  iters={result_ras['iters']:3d} conv={result_ras['converged']} "
     f"err={err_ras:.2e} res={result_ras['residual']:.2e} time={t_ras:.3f}s")
del db
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

# ── Run RAS Graph (graph-based overlap, distributed) ──
db_path = qa_tmp(f"fly_bench_rasg_{MATRIX}_n{N_SIDE}")
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)
db = open_db(db_path)

t0 = time.perf_counter()
result_rasg = solve_ras_graph(db, N, rows, cols, vals, b, NSD,
                               graph_depth=DEPTH, max_iter=200, tol=1e-8)
t_rasg = time.perf_counter() - t0

x_rasg = np.array(result_rasg["x"])
err_rasg = np.linalg.norm(x_rasg - x_exact) / np.linalg.norm(x_exact)
res_rasg = np.linalg.norm(np.array(b) - A_sp @ x_rasg) / np.linalg.norm(b)
INFO(f"RASG: iters={result_rasg['iters']:3d} conv={result_rasg['converged']} "
     f"err={err_rasg:.2e} res={res_rasg:.2e} time={t_rasg:.3f}s")
del db
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

# ── Summary ──
INFO(f"\n{'='*50}")
INFO(f"Benchmark: {MATRIX} n={N_SIDE} nsd={NSD} depth={DEPTH}")
INFO(f"  RAS  (geometric): {t_ras:.3f}s  iters={result_ras['iters']:3d}  err={err_ras:.2e}")
INFO(f"  RASG (graph):     {t_rasg:.3f}s  iters={result_rasg['iters']:3d}  err={err_rasg:.2e}")
INFO(f"  Speedup: {t_ras/t_rasg:.2f}x")
INFO(f"{'='*50}")

master.stop()
INFO("[DONE]")
