#!/usr/bin/env python3
"""Test: solve_ras_graph_v2 结果读取回归 —— 验证 _wait_solution 等 converged。

回归 docs/issues/check-daemon-shutdown-race.md 描述的竞态：_wait_solution 等
最后一个写的对象（converged），确保 sol/iters/converged 都可读，不再 EOFError。
"""
import os, time, shutil
import numpy as np
from fly import open_db, get_config
from solver.ras_graph import generate_poisson_matrix
from solver.ras_graph_daemon import solve_ras_graph_v2

N_SIDE = 20
NSD = 2
DB_PATH = os.path.join(get_config().get_str("log_dir"), "v2_cw_db")
MATRIX_PATH = os.path.join(DB_PATH, f"poisson_n{N_SIDE}.npz")


def main():
    if os.path.isdir(DB_PATH):
        shutil.rmtree(DB_PATH, ignore_errors=True)
    os.makedirs(DB_PATH, exist_ok=True)
    generate_poisson_matrix(N_SIDE, MATRIX_PATH, compute_exact=True)
    m = np.load(MATRIX_PATH, allow_pickle=False)
    n = int(m["N"])

    db = open_db(DB_PATH)
    get_config().set_int("fail_unscheduleable_tasks", 0)

    print(f"\n=== solve_ras_graph_v2 n={N_SIDE} nsd={NSD} (converged-wait regression) ===")
    t0 = time.time()
    result = solve_ras_graph_v2(db, MATRIX_PATH, NSD,
                                overlap_ratio=0.50, max_iter=100, tol=1e-8,
                                omega="coarse")
    elapsed = time.time() - t0

    assert isinstance(result, dict), f"result should be dict, got {type(result)}"
    assert "x" in result and "iters" in result and "converged" in result
    x_ras = np.asarray(result["x"])
    assert x_ras.shape == (n,), f"x shape mismatch: {x_ras.shape} vs ({n},)"
    assert int(result["iters"]) > 0
    assert isinstance(result["converged"], (bool, np.bool_))

    print(f"\n=== RESULT ===")
    print(f"  iters={int(result['iters'])} converged={bool(result['converged'])}")
    print(f"  time={elapsed:.2f}s")
    assert bool(result["converged"]), "Did not converge"
    print("[PASS] solve_ras_graph_v2 returned complete solution dict")


if __name__ == "__main__":
    main()
