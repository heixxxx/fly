#!/usr/bin/env python3
"""n=1000 ras_graph_v2 性能对比（vs 旧版 14.4s + scipy 11.5s）。"""
import os, time, numpy as np
from scipy import sparse
from fly import open_db, get_config
from solver.ras_graph import generate_poisson_matrix
from solver.ras_graph_daemon import solve_ras_graph_v2

DB_PATH = os.path.join(get_config().get_str("log_dir"), "v2_n1000_db")
MATRIX_PATH = "big_qa/matrices/poisson_n1000.npz"


def main():
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)

    m = np.load(MATRIX_PATH, allow_pickle=False)
    x_exact = np.asarray(m['x_exact'])

    db = open_db(DB_PATH)
    get_config().set_int("fail_unscheduleable_tasks", 0)

    print(f"\n=== solve_ras_graph_v2 n=1000 nsd=4 omega=coarse ===")
    t0 = time.time()
    result = solve_ras_graph_v2(db, MATRIX_PATH, nsd=4,
                                overlap_ratio=0.20, max_iter=100, tol=1e-8,
                                omega="coarse")
    elapsed = time.time() - t0

    x_ras = np.asarray(result["x"])
    rel_err = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
    print(f"\n=== RESULT v2 ===")
    print(f"  iters={result['iters']} converged={result['converged']}")
    print(f"  time={elapsed:.1f}s")
    print(f"  rel_err={rel_err:.3e}")
    print(f"  vs 旧版 14.4s: {'更快' if elapsed < 14.4 else '更慢'} ({(elapsed-14.4)/14.4*100:+.1f}%)")
    print(f"  vs scipy 11.5s: solver/scipy={elapsed/11.5:.2f}x")

    assert result["converged"], "Did not converge"
    assert rel_err < 1e-4, f"rel_err too large: {rel_err}"
    print("[PASS] v2 n=1000 converged")


if __name__ == "__main__":
    main()
