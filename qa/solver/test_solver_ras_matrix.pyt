"""RAS solver 参数矩阵（13 组合循环编排，sub case 各起独立 fly 进程）。

2026-08-16 收敛：替代 13 个 test_solver_ras_n*_sd*_ov*.pyt + 参数重复的
test_solver_ras.pyt（原 14 个 case，每文件仅 N/NSD/OVERLAP 三行不同）。
每组经 env SOLVER_N/SOLVER_NSD/SOLVER_OVERLAP 注入 solver_ras_param.py。
"""
MATRIX = [
    (4, 2, 1), (4, 2, 2),
    (6, 2, 1), (6, 2, 2), (6, 3, 1), (6, 3, 2),
    (8, 2, 1), (8, 2, 2), (8, 4, 1), (8, 4, 2),
    (10, 2, 1), (10, 2, 2), (10, 5, 1),
]

for n, nsd, ov in MATRIX:
    run_subcase("solver_ras_param.py", timeout=120,
                env={"SOLVER_N": str(n), "SOLVER_NSD": str(nsd), "SOLVER_OVERLAP": str(ov)})
    INFO(f"[matrix] n={n} sd={nsd} ov={ov} PASSED")

INFO(f"[PASS] test_solver_ras_matrix: {len(MATRIX)} combos verified")
