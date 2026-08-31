"""E2E test: SolverProject 模板（build_matrix / solve 两个 flow，纯异步范式）。

验证设计 docs/project-design.md §4：
  - build_matrix/solve 都是纯异步 flow：master 侧只做检查输入/建库/提交入口 task/
    提交 freeze task 四件轻量事，提交后立即返回 db。
  - matrix 在 db 间流转：solve 的 kickoff task inputs 依赖 matrix_db 的 matrix，
    master 调度推进求解。
  - freeze 作为 task：依赖上游数据（matrix / __rasg__sol）写完后由 master 调度执行。
  - 用户负责唤起 worker（带 sd_i attributes）；读结果用 wait_frozen 等整库 frozen。

矩阵用签入的小矩阵 poisson_n20.npz（n=20, N=400, 含 x_exact）。
"""
import os
import shutil

import numpy as np
from scipy import sparse
from _fly_log import INFO

from fly import get_config, launch_workers, wait_tasks
from fly.runtime import get_agent
from solver import SolverProject

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MATRIX_PATH = os.path.join(SCRIPT_DIR, "matrices", "poisson_n20.npz")
assert os.path.isfile(MATRIX_PATH), f"matrix file missing: {MATRIX_PATH}"

LOG_DIR = get_config().get_str("log_dir")
PROJ_PATH = os.path.join(LOG_DIR, "solver_project")
NSD = 4


def cleanup():
    if os.path.isdir(PROJ_PATH):
        shutil.rmtree(PROJ_PATH, ignore_errors=True)


def wait_for(cond, timeout=60.0, interval=0.5):
    import time
    t0 = time.time()
    while time.time() - t0 < timeout:
        if cond():
            return True
        time.sleep(interval)
    return False


cleanup()
get_config().set_int("fail_unscheduleable_tasks", 1)

# ── 用户预先唤起 solver worker（>= nsd+1 个：nsd 个 sd 宿主 + 1 个独立
# check 宿主——dynamic 架构要求 check 与 sd 分进程，setup_compute 的
# stop_peer_rpc 会关本 worker 全部 peer 连接，共存即互杀）──
# flow 不碰 worker；用户脚本负责 worker 池。
launch_workers([{"attributes": [f"sd_{i}"]} for i in range(NSD)] + [{}])
assert get_agent().wait_workers_registered(timeout=60), \
    f"{NSD} workers should connect"
INFO(f"  {NSD} workers connected (user-managed)")

# ── API 1: build_matrix（异步：提交 task 后立即返回 db）──
proj = SolverProject(PROJ_PATH)
INFO(f"registered flows: {proj.list_flows()}")
assert proj.list_flows() == ["build_matrix", "solve"], \
    f"flows={proj.list_flows()}"

matrix_db = proj.build_matrix(name="matrix", matrix_path=MATRIX_PATH)
INFO(f"[API1] build_matrix returned db (async): {matrix_db}")
# flow 返回时 matrix 还没写完、还没 freeze —— 异步进行中。

# ── API 2: solve（异步：显式传入 matrix_db，提交 kickoff task 后立即返回 db）──
# kickoff task inputs 依赖 matrix_db 的 matrix；master 在 matrix ready 后调度 kickoff
# → coord 启动迭代链 → assemble 写 __rasg__sol → freeze task 触发。
result_db = proj.solve(
    name="solve",
    matrix_db=matrix_db,          # 显式传入数据源 db
    nsd=NSD,
    overlap_ratio=0.30,
    max_iter=200,
    tol=1e-8,
    omega=1.0,
)
INFO(f"[API2] solve returned db (async): {result_db}")

# ── 等待求解 + freeze 全部完成 ──
# freeze task 依赖 __rasg__sol，故 matrix_db / result_db 都 frozen 等价于全流程完成。
assert proj.wait_frozen("matrix", timeout=120), "matrix db should freeze after matrix written"
assert proj.wait_frozen("solve", timeout=120), "solve db should freeze after solve completes"
INFO("[WAIT] both dbs frozen — full pipeline done")

# ── 校验矩阵 db ──
m = matrix_db.read_object("matrix")
assert m["N"] == 400, f"expected N=400, got {m['N']}"
assert m["n"] == 20
INFO(f"[API1] matrix readable: N={m['N']}")

# ── 校验求解结果（__rasg__sol 是对外结果对象，save_to_db=True）──
# 注意：__rasg__iters/__rasg__converged 是 solver 内部 temp 对象（save_to_db=False），
# 语义上仅限流程内部使用，外部流程/测试不该读（freeze 后会被清理，属预期）。
# 对外只读 __rasg__sol；用其相对精确解的精度校验证明收敛。
x = np.array(result_db.read_object("__rasg__sol"))
INFO(f"[API2] solve done: ||x||={np.linalg.norm(x):.4f}")

# 精度校验 vs 精确解（矩阵自带 x_exact）——精度达标即证明收敛。
data = np.load(MATRIX_PATH, allow_pickle=False)
x_exact = data["x_exact"]
rel_error = np.linalg.norm(x - x_exact) / np.linalg.norm(x_exact)
INFO(f"[API2] rel_error vs exact = {rel_error:.2e}")
assert rel_error < 1e-4, f"rel_error too large: {rel_error:.2e}"

# 残差校验 ||b - A x||。
A_sp = sparse.csc_matrix((data["vals"], (data["rows"], data["cols"])), shape=(400, 400))
rel_res = np.linalg.norm(data["b"] - A_sp @ x) / np.linalg.norm(data["b"])
INFO(f"[API2] rel_residual = {rel_res:.2e}")
assert rel_res < 1e-4, f"rel_residual too large: {rel_res:.2e}"

# ── Project 管理校验：两个 db 都记入 meta ──
assert proj.list_dbs() == ["matrix", "solve"], f"dbs={proj.list_dbs()}"
assert proj.is_db_frozen("matrix") and proj.is_db_frozen("solve"), \
    "both dbs should be frozen"

get_agent().stop()
INFO("[PASS] test_solver_project")
