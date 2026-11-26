"""RAS solver 参数矩阵（13 组参数，每组一个子阶段、各起独立 fly 进程）。

13 个入口脚本 test_solver_ras_n*_sd*_ov*.py 结构相同：设置一组参数，调用公共模块
solver_ras_case.py 的 run_solver_ras_case（问题规模 N、子域数 NSD、重叠数 OVERLAP）。
2026-08-16 的收敛把 13 个逐文件复制的脚本合并成「一个脚本 + 循环注入环境变量」；
2026-11-26 改成「公共模块 + 入口脚本」，这样每组参数各有自己的子阶段脚本名，
结果文件与基准文件可以按脚本名区分。
"""
MATRIX = [
    "test_solver_ras_n4_sd2_ov1.py",
    "test_solver_ras_n4_sd2_ov2.py",
    "test_solver_ras_n6_sd2_ov1.py",
    "test_solver_ras_n6_sd2_ov2.py",
    "test_solver_ras_n6_sd3_ov1.py",
    "test_solver_ras_n6_sd3_ov2.py",
    "test_solver_ras_n8_sd2_ov1.py",
    "test_solver_ras_n8_sd2_ov2.py",
    "test_solver_ras_n8_sd4_ov1.py",
    "test_solver_ras_n8_sd4_ov2.py",
    "test_solver_ras_n10_sd2_ov1.py",
    "test_solver_ras_n10_sd2_ov2.py",
    "test_solver_ras_n10_sd5_ov1.py",
]

# 参数只写在两处：入口脚本的文件名、入口脚本里的调用实参。这里只按脚本名逐个调度。
# 13 个脚本名互不相同，每个只调度一次；清单里的文件名按各脚本名展开。
for script in MATRIX:
    run_subcase(script, timeout=120, check_list=["result.json"])
    INFO(f"[matrix] {script[:-3]} done")

INFO("[DONE] test_solver_ras_matrix: 13 combos")
