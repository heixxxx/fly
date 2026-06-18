"""Benchmark: distributed RAS solver vs scipy baseline.

Measures:
  1. scipy sparse direct solve (single-process baseline)
  2. RAS pure iteration compute (C++ subdomain solve + residual)
  3. RAS distributed end-to-end (Fly framework overhead included)

Configs: grid sizes n=4,6,8,10, subdomain counts 2-5, overlap 1-2.
"""
from _fly_log import INFO
import sys
import os
import shutil
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                '..', 'src'))

import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

from fly import open_db, get_config
from fly.runtime import get_agent

# ── Problem construction ──

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

# ── Benchmark configs ──

CONFIGS = [
    (4, 2, 1),
    (4, 2, 2),
    (6, 2, 1),
    (6, 2, 2),
    (6, 3, 1),
    (6, 3, 2),
    (8, 2, 1),
    (8, 2, 2),
    (8, 4, 1),
    (8, 4, 2),
    (10, 2, 1),
    (10, 2, 2),
    (10, 5, 1),
    (20, 4, 1),
    (50, 4, 1),
    (50, 8, 1),
    (100, 4, 1),
    (100, 8, 1),
    (100, 16, 1),
    (200, 8, 1),
    (200, 16, 1),
    (316, 16, 1),
    (500, 16, 1),
    (700, 16, 1),
    (1000, 16, 1),
]

# ── Phase 1: scipy baseline ──

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
    repeats = max(1, min(100, 1000 // max(1, N // 100)))
    t0 = time.perf_counter()
    for _ in range(repeats):
        x_ref = spla.spsolve(A, b)
    elapsed = (time.perf_counter() - t0) / repeats * 1000

    scipy_results[n] = elapsed
    INFO(f"  n={n:2d} (N={N:4d}): scipy={elapsed:.3f} ms ({repeats} repeats)")

# ── Phase 2: RAS pure C++ compute benchmark ──

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

    # Build subdomain solvers
    solvers = []
    infos = []
    for sd in sds:
        _, _, lr, lc, lv = ex_slv_extract_subdomain_matrix(size, rows, cols, vals, sd.local_indices)
        solver = EXSlvSubdomainSolver.from_coo(len(sd.local_indices), lr, lc, lv)
        solvers.append(solver)
        info = EXSlvSubdomainInfo()
        info.subdomain_id = sd.subdomain_id
        info.local_indices = sd.local_indices
        info.own_indices = sd.own_indices
        info.boundary_indices = sd.boundary_indices
        infos.append(info)

    b = [1.0] * size
    x = [0.0] * size

    # Run RAS iterations pure C++
    maxiter = 100
    tol = 1e-4
    iters_done = 0

    t0 = time.perf_counter()
    for it in range(maxiter):
        # Subdomain solves
        results = []
        for i in range(nsd):
            x_new = ex_slv_ras_subdomain_update(size, rows, cols, vals, b, x, infos[i], solvers[i])
            results.append((infos[i].own_indices, x_new))

        # Update x
        for own, x_new in results:
            for idx in own:
                x[idx] = x_new[idx]

        # Residual check
        res = ex_slv_residual_norm(size, rows, cols, vals, x, b)
        iters_done = it + 1
        if res < tol:
            break

    elapsed = (time.perf_counter() - t0) * 1000  # ms

    INFO(f"  n={n:2d} sd={nsd} ov={ov}: "
         f"iters={iters_done:3d}, res={res:.2e}, "
         f"pure_cpp={elapsed:.1f} ms, "
         f"per_iter={elapsed/iters_done:.2f} ms, "
         f"scipy={scipy_results[n]:.3f} ms")

# ── Phase 3: Distributed RAS end-to-end ──

INFO("")
INFO("=" * 70)
INFO("Phase 3: Distributed RAS end-to-end (Fly framework)")
INFO("=" * 70)

# Find max workers needed
max_workers = max(nsd for _, nsd, _ in CONFIGS)

master = get_agent()
master.launch_local_workers([{}] * max_workers)
assert master.wait_for_workers(max_workers), f"{max_workers} workers should connect"
INFO(f"  {max_workers} workers connected")

for n, nsd, ov in CONFIGS:
    db_path = f"/tmp/fly_bench_ras_n{n}_sd{nsd}_ov{ov}"
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    db = open_db(db_path)
    from solver import solve_ras

    t0 = time.perf_counter()
    result = solve_ras(db, n, nsd, ov)
    elapsed = (time.perf_counter() - t0) * 1000  # ms

    INFO(f"  n={n:2d} sd={nsd} ov={ov}: "
         f"iters={result['iters']:3d}, res={result['residual']:.2e}, "
         f"dist_total={elapsed:.0f} ms, "
         f"scipy={scipy_results[n]:.3f} ms, "
         f"overhead={elapsed - scipy_results[n]:.0f} ms")

master.stop()
INFO("")
INFO("[DONE] bench_solver_ras")
