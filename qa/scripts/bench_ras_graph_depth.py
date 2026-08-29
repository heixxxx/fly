"""Benchmark: RASG convergence vs depth, with node expansion ratio.

Usage:
  ./build/bin/fly qa/bench_ras_graph_depth.py
  BN=50 BNSD=4 ./build/bin/fly qa/bench_ras_graph_depth.py
"""
from _fly_log import INFO
from test import qa_tmp
import os
import shutil
import time
import math
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import splu


from fly import open_db, get_config
from fly.runtime import get_agent
from solver import solve_ras_graph
from _fly_solver import ex_slv_graph_expand_overlap

get_config().set_int("fail_unscheduleable_tasks", 1)

N_SIDE = int(os.environ.get("BN", "100"))
NSD = int(os.environ.get("BNSD", "4"))
DEPTHS = [2, 3, 4, 5]


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


def partition_primary(n, nsd):
    n_side = int(math.isqrt(n))
    base = n_side // nsd
    rem = n_side % nsd
    sets = []
    rs = 0
    for p in range(nsd):
        nr = base + (1 if p < rem else 0)
        re = rs + nr
        sets.append(list(range(rs * n_side, re * n_side)))
        rs = re
    return sets


# ── Build matrix ──
N, rows, cols, vals = build_poisson_2d(N_SIDE)
b = [1.0] * N

A_sp = sparse.csc_matrix((vals, (rows, cols)), shape=(N, N))
x_exact = splu(A_sp).solve(np.array(b))

primary_sets = partition_primary(N, NSD)

# ── Sparsity stats ──
nnz = len(rows)
avg_degree = nnz / N
INFO(f"Matrix: N={N} nnz={nnz} avg_degree={avg_degree:.1f} density={nnz/N/N:.2e}")

# ── Node expansion ratio per depth ──
INFO(f"{'='*70}")
INFO(f"Poisson n={N_SIDE} N={N} nsd={NSD}")
INFO(f"{'='*70}")
INFO(f"{'Depth':>5} | {'Primary':>8} | {'Extended':>9} | {'Ratio':>6} | "
     f"{'Iters':>5} | {'Time':>6} | {'Error':>10} | {'Conv':>4}")
INFO(f"{'-'*5}-+-{'-'*8}-+-{'-'*9}-+-{'-'*6}-+-"
     f"{'-'*5}-+-{'-'*6}-+-{'-'*10}-+-{'-'*4}")

results = []

for depth in DEPTHS:
    # Compute expansion ratio
    ratios = []
    primary_sizes = []
    extended_sizes = []
    for sd_id in range(NSD):
        ps = primary_sets[sd_id]
        primary_sizes.append(len(ps))
        ext = ex_slv_graph_expand_overlap(N, rows, cols, vals, ps, depth)
        extended_sizes.append(len(ext))
        ratios.append(len(ext) / len(ps) if len(ps) > 0 else 0)

    avg_ratio = sum(ratios) / len(ratios)
    avg_primary = sum(primary_sizes) // NSD
    avg_extended = sum(extended_sizes) // NSD

    # Run distributed solve
    db_path = qa_tmp(f"fly_bench_rasg_depth_n{N_SIDE}_d{depth}")
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    master = get_agent()
    master.launch_local_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)])
    assert master.wait_for_workers(NSD), "workers should connect"

    db = open_db(db_path)
    t0 = time.perf_counter()
    sol = solve_ras_graph(db, N, rows, cols, vals, b, NSD,
                           graph_depth=depth, max_iter=300, tol=1e-8)
    elapsed = time.perf_counter() - t0

    x_rasg = np.array(sol["x"])
    err = np.linalg.norm(x_rasg - x_exact) / np.linalg.norm(x_exact)
    iters = sol["iters"]
    conv = sol["converged"]

    master.stop()

    del db
    if os.path.isdir(db_path):
        shutil.rmtree(db_path, ignore_errors=True)

    INFO(f"{depth:>5} | {avg_primary:>8} | {avg_extended:>9} | "
         f"{avg_ratio:>5.2f}x | {iters:>5} | {elapsed:>5.2f}s | "
         f"{err:>10.2e} | {'Y' if conv else 'N':>4}")

    results.append({
        "depth": depth, "primary": avg_primary, "extended": avg_extended,
        "ratio": avg_ratio, "iters": iters, "time": elapsed,
        "error": err, "converged": conv,
    })

INFO(f"{'='*70}")
INFO(f"Per-subdomain breakdown (last depth={DEPTHS[-1]}):")
for sd_id in range(NSD):
    ps = primary_sets[sd_id]
    ext = ex_slv_graph_expand_overlap(N, rows, cols, vals, ps, DEPTHS[-1])
    INFO(f"  sd_{sd_id}: primary={len(ps)} extended={len(ext)} "
         f"ratio={len(ext)/len(ps):.2f}x")

INFO("[DONE]")
