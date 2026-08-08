"""Benchmark: scipy vs RAS pure C++ vs RAS Graph (default) vs RAS Graph (coarse).

Measures:
  1. scipy sparse direct solve (baseline)
  2. RAS pure C++ iteration (no framework overhead)
  3. RAS Graph default (distributed, with framework)
  4. RAS Graph coarse (distributed, with framework + coarse correction)

Usage:
  ./fly.sh build //src/main/cpp:fly && ./fly.sh install
  ./build/bin/fly qa/scripts/bench_n500.py
"""
from _fly_log import INFO
import os
import shutil
import time
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu

from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph, generate_poisson_matrix

get_config().set_int("fail_unscheduleable_tasks", 1)

N_SIDE = 500
NSD = 4
OVERLAP_RATIO = 0.30
MAX_ITER = 300
TOL = 1e-8

# ── Generate matrix ──
INFO(f"Generating matrix: n={N_SIDE} N={N_SIDE*N_SIDE}")
matrix_path = f"/tmp/fly_bench_matrix_n{N_SIDE}.npz"
generate_poisson_matrix(N_SIDE, matrix_path)
golden = np.load(matrix_path, allow_pickle=False)
x_exact = golden["x_exact"]
rows, cols, vals = golden["rows"], golden["cols"], golden["vals"]
b = golden["b"]
N_val = int(golden["N"])
A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N_val, N_val))

INFO(f"Matrix: N={N_val}, nnz={A_sp.nnz}")

# ── Phase 1: scipy baseline ──
INFO("")
INFO("=" * 60)
INFO("Phase 1: scipy sparse direct solve (SPLU)")
INFO("=" * 60)

t0 = time.perf_counter()
x_scipy = splu(A_sp).solve(np.array(b))
t_scipy = time.perf_counter() - t0
err_scipy = np.linalg.norm(x_scipy - x_exact) / np.linalg.norm(x_exact)
INFO(f"  scipy SPLU: {t_scipy:.3f}s  err={err_scipy:.2e}")

# ── Phase 2: RAS pure C++ per-iteration cost (no framework) ──
INFO("")
INFO("=" * 60)
INFO("Phase 2: RAS pure C++ per-iteration cost (no framework)")
INFO("=" * 60)

from _fly_solver import (
    ex_slv_build_poisson_2d, ex_slv_partition_1d,
    ex_slv_extract_subdomain_matrix,
    EXSlvSubdomainSolver, EXSlvSubdomainInfo,
    ex_slv_ras_subdomain_update, ex_slv_residual_norm,
)

# Use smaller n for pure C++ (single-process, not distributed)
PURE_N = min(N_SIDE, 100)
INFO(f"  Using n={PURE_N} (N={PURE_N*PURE_N}) for pure C++ test")
size, _, p_rows, p_cols, p_vals = ex_slv_build_poisson_2d(PURE_N)
sds = ex_slv_partition_1d(PURE_N, NSD, 1)  # overlap=1

solvers = []
infos = []
for sd in sds:
    _, _, lr, lc, lv = ex_slv_extract_subdomain_matrix(size, p_rows, p_cols, p_vals, sd.local_indices)
    solver = EXSlvSubdomainSolver.from_coo(len(sd.local_indices), lr, lc, lv)
    solvers.append(solver)
    info = EXSlvSubdomainInfo()
    info.subdomain_id = sd.subdomain_id
    info.local_indices = sd.local_indices
    info.own_indices = sd.own_indices
    info.boundary_indices = sd.boundary_indices
    infos.append(info)

# Measure per-iteration cost (run a few iterations, don't wait for convergence)
x_pure = [0.0] * size
b_pure = [1.0] * size
WARMUP = 2
MEASURE = 5

for _ in range(WARMUP):
    results = []
    for i in range(NSD):
        x_new = ex_slv_ras_subdomain_update(size, p_rows, p_cols, p_vals, b_pure, x_pure, infos[i], solvers[i])
        results.append((infos[i].own_indices, x_new))
    for own, x_new in results:
        for idx in own:
            x_pure[idx] = x_new[idx]

t_pure_start = time.perf_counter()
for _ in range(MEASURE):
    results = []
    for i in range(NSD):
        x_new = ex_slv_ras_subdomain_update(size, p_rows, p_cols, p_vals, b_pure, x_pure, infos[i], solvers[i])
        results.append((infos[i].own_indices, x_new))
    for own, x_new in results:
        for idx in own:
            x_pure[idx] = x_new[idx]
t_pure_iter = time.perf_counter() - t_pure_start

per_iter_ms = t_pure_iter / MEASURE * 1000
INFO(f"  RAS pure C++: {per_iter_ms:.2f} ms/iter ({MEASURE} iters, n={PURE_N})")

# Extrapolate to N_SIDE
scale = (N_SIDE / PURE_N) ** 2  # cost scales with matrix size
per_iter_n500 = per_iter_ms * scale
INFO(f"  Estimated per-iter at n={N_SIDE}: {per_iter_n500:.2f} ms")

# ── Phase 3: RAS Graph default (omega=1.0) ──
INFO("")
INFO("=" * 60)
INFO("Phase 3: RAS Graph (default, omega=1.0)")
INFO("=" * 60)

master = get_agent()
master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
assert master.wait_for_workers(NSD), f"{NSD} workers should connect"

db_path = f"/tmp/fly_bench_n{N_SIDE}_sd{NSD}_default"
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

db = open_db(db_path)
t0 = time.perf_counter()
sol_default = solve_ras_graph(db, matrix_path, NSD,
                              overlap_ratio=OVERLAP_RATIO,
                              max_iter=MAX_ITER, tol=TOL,
                              omega=1.0)
t_default = time.perf_counter() - t0

x_default = np.array(sol_default["x"])
err_default = np.linalg.norm(x_default - x_exact) / np.linalg.norm(x_exact)
res_default = np.linalg.norm(b - A_sp @ x_default) / np.linalg.norm(b)

INFO(f"  RAS default: {t_default:.3f}s  iters={sol_default['iters']}  "
     f"err={err_default:.2e}  res={res_default:.2e}")

del db
shutil.rmtree(db_path, ignore_errors=True)

# ── Phase 4: RAS Graph coarse ──
INFO("")
INFO("=" * 60)
INFO("Phase 4: RAS Graph (coarse)")
INFO("=" * 60)

db_path = f"/tmp/fly_bench_n{N_SIDE}_sd{NSD}_coarse"
if os.path.isdir(db_path):
    shutil.rmtree(db_path, ignore_errors=True)

db = open_db(db_path)
t0 = time.perf_counter()
sol_coarse = solve_ras_graph(db, matrix_path, NSD,
                             overlap_ratio=OVERLAP_RATIO,
                             max_iter=MAX_ITER, tol=TOL,
                             omega="coarse")
t_coarse = time.perf_counter() - t0

x_coarse = np.array(sol_coarse["x"])
err_coarse = np.linalg.norm(x_coarse - x_exact) / np.linalg.norm(x_exact)
res_coarse = np.linalg.norm(b - A_sp @ x_coarse) / np.linalg.norm(b)

INFO(f"  RAS coarse: {t_coarse:.3f}s  iters={sol_coarse['iters']}  "
     f"err={err_coarse:.2e}  res={res_coarse:.2e}")

del db
shutil.rmtree(db_path, ignore_errors=True)

# ── Summary ──
INFO("")
INFO("=" * 60)
INFO(f"Summary: n={N_SIDE} N={N_val} nsd={NSD} overlap={OVERLAP_RATIO}")
INFO("=" * 60)
INFO(f"  scipy SPLU:      {t_scipy:.3f}s  (single-process direct)")
INFO(f"  RAS pure C++:    {t_pure_iter:.3f}s  iters={iteration+1}  (no framework)")
INFO(f"  RAS default:     {t_default:.3f}s  iters={sol_default['iters']}")
INFO(f"  RAS coarse:      {t_coarse:.3f}s  iters={sol_coarse['iters']}")
INFO("")
INFO(f"  Framework overhead (default vs pure): {t_default/t_pure_iter:.2f}x")
INFO(f"  Framework overhead (coarse vs pure):  {t_coarse/t_pure_iter:.2f}x")
INFO(f"  RAS coarse / default:                 {t_default/t_coarse:.2f}x faster")
INFO("")
INFO(f"  RAS default / scipy:  {t_default/t_scipy:.2f}x")
INFO(f"  RAS coarse / scipy:   {t_coarse/t_scipy:.2f}x")
INFO("=" * 60)

master.stop()
INFO("[DONE]")
