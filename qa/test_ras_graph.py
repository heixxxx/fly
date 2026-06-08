"""RAS Graph (graph-based overlap RAS) solver: Poisson n=20, subdomains=4, depth=2.

Usage:
  ./fly.sh build //src/main/cpp:fly && ./fly.sh install
  bash qa/run_qa_tests.sh qa/test_ras_graph.py
"""
from _fly_log import INFO
import sys
import os
import shutil
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph

N_SIDE = 20
NSD = 4
DB_PATH = "/tmp/fly_e2e_ras_graph_db"


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


# ── cleanup ──
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)

# ── build matrix ──
N = N_SIDE * N_SIDE
N_val, rows, cols, vals = build_poisson_2d(N_SIDE)
b = [1.0] * N_val

# ── exact solution ──
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))
x_exact = splu(A_sp).solve(np.array(b))

# ── solve via ras_graph ──
db = open_db(DB_PATH)
sol = solve_ras_graph(db, N_val, rows, cols, vals, b, NSD,
                      overlap_ratio=0.30, max_iter=100, tol=1e-8)

x_ras = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]

rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
max_error = np.max(np.abs(x_ras - x_exact))
rel_res = np.linalg.norm(np.array(b) - A_sp @ x_ras) / np.linalg.norm(b)

INFO(f"iters={iters} converged={converged} "
     f"rel_err={rel_error:.2e} max_err={max_error:.2e} "
     f"rel_res={rel_res:.2e}")

assert converged, f"Did not converge: iters={iters}"
assert rel_error < 1e-4, f"rel_error too large: {rel_error:.2e}"
assert rel_res < 1e-4, f"rel_residual too large: {rel_res:.2e}"

get_agent().stop()
INFO(f"[PASS] test_ras_graph n={N_SIDE} nsd={NSD}")
