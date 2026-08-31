#!/usr/bin/env python3
"""Test: dynamic 单步端到端 n=50 小规模验证（原 v2 daemon case，求解器收敛迁移）。"""
import os, sys, time
from scipy import sparse
from scipy.sparse.linalg import splu
import numpy as np

from fly import open_db, get_config
from solver import generate_poisson_matrix
from solver import solve_once, MATRIX_OBJ_KEY, SolveDb

N_SIDE = 50
NSD = 4
DB_PATH = os.path.join(get_config().get_str("log_dir"), "v2_db")
MATRIX_PATH = os.path.join(DB_PATH, f"poisson_n{N_SIDE}.npz")


def main():
    import shutil
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)
    os.makedirs(DB_PATH, exist_ok=True)

    # 生成矩阵
    generate_poisson_matrix(N_SIDE, MATRIX_PATH, compute_exact=True)

    # scipy 基线
    m = np.load(MATRIX_PATH, allow_pickle=False)
    _md = {k: m[k] for k in m.files}
    A = sparse.csr_matrix((np.asarray(m['vals']), (np.asarray(m['rows']), np.asarray(m['cols']))),
                          shape=(int(m['N']), int(m['N'])))
    x_exact = np.asarray(m['x_exact'])
    b = np.asarray(m['b'])

    db = open_db(DB_PATH, db_cls=SolveDb)
    get_config().set_int("fail_unscheduleable_tasks", 0)

    print(f"\n=== solve_once n={N_SIDE} nsd={NSD} omega=coarse ===")
    t0 = time.time()
    db.write_object(MATRIX_OBJ_KEY, _md)
    result = solve_once(db, MATRIX_OBJ_KEY, NSD,
                        overlap_ratio=0.50, max_iter=100, tol=1e-8,
                        omega="coarse")
    elapsed = time.time() - t0

    x_ras = np.asarray(result["x"])
    rel_err = np.linalg.norm(x_ras - x_exact) / np.linalg.norm(x_exact)
    print("\n=== RESULT ===")
    print(f"  iters={result['iters']} converged={result['converged']}")
    print(f"  time={elapsed:.2f}s")
    print(f"  rel_err={rel_err:.3e}")

    assert result["converged"], "Did not converge"
    assert rel_err < 1e-4, f"rel_err too large: {rel_err}"
    print("[PASS] solve_once n=50 converged with acceptable accuracy")


if __name__ == "__main__":
    main()
