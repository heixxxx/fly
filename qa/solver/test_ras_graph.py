"""RAS Graph solver: Poisson n=20, nsd=4, with matrix file input.

Usage:
  ./fly.sh build //src/main/cpp:fly && ./fly.sh install
  bash qa/run_qa_tests.sh qa/test_ras_graph.py
"""
from _fly_log import INFO
import os
import shutil
import numpy as np


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph, generate_poisson_matrix, MATRIX_OBJ_KEY, SolveDb

N_SIDE = 20
NSD = 4
DB_PATH = f"/tmp/fly_e2e_ras_graph_db_{os.getpid()}"
MATRIX_PATH = f"/tmp/fly_e2e_ras_graph_matrix_{os.getpid()}.npz"

# ── cleanup ──
if os.path.isdir(DB_PATH):
    shutil.rmtree(DB_PATH, ignore_errors=True)

get_config().set_int("fail_unscheduleable_tasks", 1)

# ── generate matrix（进程私有临时文件，读入后即与文件解耦）──
generate_poisson_matrix(N_SIDE, MATRIX_PATH)
_data = np.load(MATRIX_PATH, allow_pickle=False)
golden = {k: _data[k] for k in _data.files}
x_exact = golden["x_exact"]

# ── 矩阵入库：分布式对象，worker 经框架路径读取 ──
db = open_db(DB_PATH, db_cls=SolveDb)
db.write_object(MATRIX_OBJ_KEY, golden)
sol = solve_ras_graph(db, MATRIX_OBJ_KEY, NSD,
                      overlap_ratio=0.30, max_iter=100, tol=1e-8)

x_ras = np.array(sol["x"])
iters = sol["iters"]
converged = sol["converged"]

rel_error = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
max_error = np.max(np.abs(x_ras - x_exact))

# ── load matrix for residual check ──
from scipy import sparse
rows = golden["rows"]
cols = golden["cols"]
vals = golden["vals"]
b = golden["b"]
N = int(golden["N"])
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N, N))
rel_res = np.linalg.norm(b - A_sp @ x_ras) / np.linalg.norm(b)

INFO(f"iters={iters} converged={converged} "
     f"rel_err={rel_error:.2e} max_err={max_error:.2e} "
     f"rel_res={rel_res:.2e}")

assert converged, f"Did not converge: iters={iters}"
assert rel_error < 1e-4, f"rel_error too large: {rel_error:.2e}"
assert rel_res < 1e-4, f"rel_residual too large: {rel_res:.2e}"

get_agent().stop()
INFO(f"[PASS] test_ras_graph n={N_SIDE} nsd={NSD}")
