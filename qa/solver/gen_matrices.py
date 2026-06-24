"""Pre-generate Poisson matrices with exact solutions into matrices/.

Run once (offline) to (re)create the .npz files used by golden_solver:
    ./build/bin/fly qa/solver/gen_matrices.py
"""
import os
import time
import numpy as np
from _fly_log import INFO
from solver import generate_poisson_matrix

OUT_DIR = os.path.join(os.path.dirname(__file__), "matrices")
os.makedirs(OUT_DIR, exist_ok=True)

for n in [20, 30, 50, 500, 1000]:
    out = os.path.join(OUT_DIR, f"poisson_n{n}.npz")
    tmp = f"/tmp/gen_n{n}_{os.getpid()}.npz"
    t0 = time.perf_counter()
    generate_poisson_matrix(n, tmp, compute_exact=True)
    os.replace(tmp, out)
    data = np.load(out, allow_pickle=False)
    INFO(f"[GEN] n={n} N={int(data['N'])} nnz={len(data['rows'])} → {out} "
         f"({(time.perf_counter()-t0)*1000:.0f}ms)")
INFO("[GEN] done.")
