#!/usr/bin/env python3
"""Test: 求解结果完整读取回归（solve_once 收敛等待语义）。

历史：v2 daemon _wait_solution 竞态回归（check-daemon-shutdown-race.md）。
求解器收敛（2026-08-31）后该语义由 dynamic 的 get_dynamic_result
（wait_obj __rasg__dynamic_done）承接——本 case 验证结果 dict 完整可读
（x/iters/converged），不再 EOFError。
"""
import os, time, shutil
import numpy as np
from fly import open_db, get_config
from solver import generate_poisson_matrix, solve_once, MATRIX_OBJ_KEY, SolveDb

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
    _md = {k: m[k] for k in m.files}

    db = open_db(DB_PATH, db_cls=SolveDb)
    get_config().set_int("fail_unscheduleable_tasks", 0)

    print(f"\n=== solve_once n={N_SIDE} nsd={NSD} (converged-wait regression) ===")
    t0 = time.time()
    db.write_object(MATRIX_OBJ_KEY, _md)
    result = solve_once(db, MATRIX_OBJ_KEY, NSD,
                        overlap_ratio=0.50, max_iter=100, tol=1e-8,
                        omega="coarse")
    elapsed = time.time() - t0

    assert isinstance(result, dict), f"result should be dict, got {type(result)}"
    assert "x" in result and "iters" in result and "converged" in result
    x_ras = np.asarray(result["x"])
    assert x_ras.shape == (n,), f"x shape mismatch: {x_ras.shape} vs ({n},)"
    assert int(result["iters"]) > 0
    assert isinstance(result["converged"], (bool, np.bool_))

    print("\n=== RESULT ===")
    print(f"  iters={int(result['iters'])} converged={bool(result['converged'])}")
    print(f"  time={elapsed:.2f}s")
    assert bool(result["converged"]), "Did not converge"
    print("[PASS] solve_once returned complete solution dict")


if __name__ == "__main__":
    main()
