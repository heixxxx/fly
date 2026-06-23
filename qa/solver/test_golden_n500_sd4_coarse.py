from golden_solver import run_golden
# tol=1e-5: 收敛判断阈值。验证要求 rel_error/rel_res < 1e-4，tol=1e-5 余量
# 充足（coarse RAS 收敛后 rel_error ~1e-13），且比 1e-8 提前收敛，降低 wall time。
run_golden(500, 4, 0.30, max_iter=300, tol=1e-5, omega="coarse")
