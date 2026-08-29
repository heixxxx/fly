"""Benchmark: distributed RAS solver vs scipy baseline for LARGE matrices.

Measures:
  1. scipy sparse direct solve (single-process baseline)
  2. RAS pure iteration compute (C++ subdomain solve + residual)
  3. RAS distributed end-to-end (Fly framework overhead included)

Configs: large grid sizes n=20,50,100,200,316
"""
from _fly_log import INFO
from test import qa_tmp
import os
import shutil
import time


import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

from fly import open_db, get_config
from fly.runtime import get_agent

def build_poisson_2d(n):
    N = n * n
    rows, cols, vals = [], [], []
    for i in range(n):
        for j in range(n):
            k = i * n + j
            rows.append(k); cols.append(k); vals.append(4.0)
            if i > 0: rows.append(k); cols.append((i-1)*n+j); vals.append(-1.0)
            if i < n-1: rows.append(k); cols.append((i+1)*n+j); vals.append(-1.0)
            if j > 0: rows.append(k); cols.append(i*n+(j-1)); vals.append(-1.0)
            if j < n-1: rows.append(k); cols.append(i*n+(j+1)); vals.append(-1.0)
    return rows, cols, vals

CONFIGS = [
    (20, 4, 1),
    (50, 4, 1),
    (50, 8, 1),
    (100, 4, 1),
    (100, 8, 1),
    (100, 16, 1),
    (200, 8, 1),
    (200, 16, 1),
    (316, 16, 1),
]

INFO("=" * 70)
INFO("Phase 1: scipy sparse direct solve (single-process baseline)")
INFO("=" * 70)

scipy_results = {}
for n, nsd, ov in CONFIGS:
    N = n * n
    rows, cols, vals = build_poisson_2d(n)
    A = sp.csr_matrix((vals, (rows, cols)), shape=(N, N))
    b = np.ones(N)

    spla.spsolve(A, b)
    repeats = max(1, 100 // max(1, N // 100))
    t0 = time.perf_counter()
    for _ in range(repeats):
        x_ref = spla.spsolve(A, b)
    elapsed = (time.perf_counter() - t0) / repeats * 1000

    scipy_results[n] = elapsed
    INFO(f"  n={n:3d} (N={N:6d}): scipy={elapsed:.3f} ms ({repeats} repeats)")

INFO("")
INFO("=" * 70)
INFO("Phase 2: RAS pure C++ iteration compute (no framework overhead)")
INFO("=" * 70)

from _fly_solver import (
    ex_slv_build_poisson_2d, ex_slv_partition_1d,
    ex_slv_extract_subdomain_matrix,
    EXSlvSubdomainSolver, EXSlvSubdomainInfo,
    ex_slv_ras_subdomain_update, ex_slv_residual_norm,
)

for n, nsd, ov in CONFIGS:
    N = n * n
    size, _, rows, cols, vals = ex_slv_build_poisson_2d(n)
    sds = ex_slv_partition_1d(n, nsd, ov)

    solvers = []
    infos = []
    for sd in sds:
        _, _, lr, lc, lv = ex_slv_extract_subdomain_matrix(size, rows, cols, vals, sd.local_indices)
        solver = EXSlvSubdomainSolver.from_coo(len(sd.local_indices), lr, lc, lv)
        solvers.append(solver)
        info = EXSlvSubdomainInfo()
        info.subdomain_id = sd.subdomain_id
        info.local_indices = sd.local_indices
        info.own_indices = sd.own
        info.boundary_indices = sd.bnd
        infos.append(info)

    b = [1.0] * size
    x = [0.0] * size
    maxiter = 100
    tol = 1e-4
    iters_done = 0
    res = 0.0

    t0 = time.perf_counter()
    for it in range(maxiter):
        results = []
        for i in range(nsd):
            x_new = ex_slv_ras_subdomain_update(size, rows, cols, vals, b, x, infos[i], solvers[i])
            results.append((infos[i].own_indices, x_new))
        for own, x_new in results:
            for idx in own:
                x[idx] = x_new[idx]
        res = ex_slv_residual_norm(size, rows, cols, vals, x, b)
        iters_done = it + 1
        if res < tol:
            break
    elapsed = (time.perf_counter() - t0) * 1000

    INFO(f"  n={n:3d} sd={nsd:2d} ov={ov}: "
         f"iters={iters_done:3d}, res={res:.2e}, "
         f"pure_cpp={elapsed:.1f} ms, "
         f"per_iter={elapsed/iters_done:.2f} ms, "
         f"scipy={scipy_results[n]:.3f} ms")

INFO("")
INFO("=" * 70)
INFO("Phase 3: Distributed RAS end-to-end (Fly framework)")
INFO("=" * 70)

max_workers = max(nsd for _, nsd, _ in CONFIGS)

master = get_agent()
master.launch_local_workers([{}] * max_workers)
assert master.wait_for_workers(max_workers), f"{max_workers} workers should connect"
INFO(f"  {max_workers} workers connected")

for n, nsd, ov in CONFIGS:
    db_path = qa_tmp(f"fly_bench_large_n{n}_sd{nsd}_ov{ov}")
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    db = open_db(db_path)
    from solver import solve_ras

    t0 = time.perf_counter()
    result = solve_ras(db, n, nsd, ov)
    elapsed = (time.perf_counter() - t0) * 1000

    INFO(f"  n={n:3d} sd={nsd:2d} ov={ov}: "
         f"iters={result['iters']:3d}, res={result['residual']:.2e}, "
         f"dist_total={elapsed:.0f} ms, "
         f"per_iter={elapsed/result['iters']:.1f} ms, "
         f"scipy={scipy_results[n]:.3f} ms, "
         f"overhead={elapsed - scipy_results[n]:.0f} ms")

master.stop()
INFO("")
INFO("[DONE] bench_solver_ras_large")
