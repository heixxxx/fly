"""E2E test: UserDoc / Schema / document / help 系统（集成冒烟，无需 worker）。

验证：
  - help("solve") 输出含 prototype、参数 nsd、owner SolverProject。
  - 参数校验：非法参数抛聚合 ValueError，合法参数正常（solve 因缺 worker 不实际执行，
    只测到校验通过后 flow 体执行前的 _create_db 即可——本测试用 SolverProject 实例
    触发校验，预期在 _create_db 前被 schema 拦截或正常建库）。
  - build_matrix 非法 name 在 schema 层被拦截。

注意：solve 校验通过后会进入 flow 体（_create_db → 提交 task）。
本测试只验证**校验失败**路径和 help 输出；校验通过的端到端流程见 qa/solver/。
"""
import os

from _fly_log import INFO

from test import record
from fly import help
from solver import SolverProject


# ── help 系统输出 ────────────────────────────────────────────────────────

# help("solve") 应输出详情（含 prototype / 参数 / owner）
import io
import contextlib

buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    help("solve")
solve_help = buf.getvalue()
mentions_solve = "solve" in solve_help
# 原断言：assert "solve" in solve_help, "help('solve') should mention solve"
record("help_solve/mentions_solve", mentions_solve, detail=solve_help)
mentions_nsd = "nsd" in solve_help
# 原断言：assert "nsd" in solve_help, "help('solve') should list nsd param"
record("help_solve/mentions_nsd", mentions_nsd, detail=solve_help)
mentions_prototype = "Prototype" in solve_help
# 原断言：assert "Prototype" in solve_help, "help('solve') should show prototype"
record("help_solve/mentions_prototype", mentions_prototype, detail=solve_help)
mentions_owner = "SolverProject" in solve_help
# 原断言：assert "SolverProject" in solve_help, "help('solve') should show owner"
record("help_solve/mentions_owner", mentions_owner, detail=solve_help)
INFO("[DONE] help('solve') 输出含 prototype / nsd / SolverProject")

# help() 列出全部
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    help()
all_help = buf.getvalue()
all_mentions_solve = "solve" in all_help
# 原断言：assert "solve" in all_help
record("help_all/mentions_solve", all_mentions_solve, detail=all_help)
all_mentions_build_matrix = "build_matrix" in all_help
# 原断言：assert "build_matrix" in all_help
record("help_all/mentions_build_matrix", all_mentions_build_matrix, detail=all_help)
INFO("[DONE] help() 列出 build_matrix / solve")


# ── 参数校验：solve 非法参数聚合报错 ────────────────────────────────────

PROJ_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "test_userdoc_proj")
proj = SolverProject(PROJ_PATH)

# 场景 1：多参数同时非法 → 聚合错误
agg_type = None
agg_mentions_matrix_db = False
agg_mentions_nsd = False
agg_mentions_overlap = False
try:
    proj.solve(name="s", matrix_db=None, nsd=0, overlap_ratio=2.0)
    # 保留：没抛异常时的立即失败（否定性检查），不依赖记录
    assert False, "solve with invalid args should raise ValueError"
except ValueError as e:
    msg = str(e)
    agg_type = type(e).__name__
    # matrix_db None → 类型错误（expected _Database）
    agg_mentions_matrix_db = "matrix_db" in msg
    # nsd=0 → check 错误
    agg_mentions_nsd = "nsd" in msg and ">= 1" in msg
    # overlap_ratio=2.0 → check 错误
    agg_mentions_overlap = "overlap_ratio" in msg
# 原断言：assert "matrix_db" in msg, f"should report matrix_db: {msg}"
record("solve_agg_invalid/error_type", agg_type)
record("solve_agg_invalid/mentions_matrix_db", agg_mentions_matrix_db, detail=msg if agg_type else None)
# 原断言：assert "nsd" in msg and ">= 1" in msg, f"should report nsd: {msg}"
record("solve_agg_invalid/mentions_nsd_bounds", agg_mentions_nsd)
# 原断言：assert "overlap_ratio" in msg, f"should report overlap_ratio: {msg}"
record("solve_agg_invalid/mentions_overlap_ratio", agg_mentions_overlap)
INFO("[DONE] solve 多参数非法聚合报错")

# 场景 2：omega 非法字符串 → any_of 两分支都失败
omega_type = None
omega_mentioned = False
omega_hints_valid = False
try:
    proj.solve(name="s", matrix_db=None, nsd=4, omega="bad")
    # 保留：没抛异常时的立即失败（否定性检查），不依赖记录
    assert False
except ValueError as e:
    msg = str(e)
    omega_type = type(e).__name__
    omega_mentioned = "omega" in msg
    omega_hints_valid = "coarse" in msg or "adaptive" in msg
# 原断言：assert "omega" in msg
record("solve_omega_invalid/error_type", omega_type)
record("solve_omega_invalid/mentions_omega", omega_mentioned, detail=msg if omega_type else None)
# 原断言：assert "coarse" in msg or "adaptive" in msg, f"should hint valid omega: {msg}"
record("solve_omega_invalid/hints_valid_values", omega_hints_valid)
INFO("[DONE] solve omega 非法 any_of 报错")

# 场景 3：build_matrix 空 name → schema 拦截
empty_name_type = None
empty_name_mentioned = False
try:
    proj.build_matrix(name="", matrix_path="x.npz")
    # 保留：没抛异常时的立即失败（否定性检查），不依赖记录
    assert False
except ValueError as e:
    empty_name_type = type(e).__name__
    empty_name_mentioned = "name" in str(e)
# 原断言：assert "name" in str(e)
record("build_matrix_empty_name/error_type", empty_name_type)
record("build_matrix_empty_name/mentions_name", empty_name_mentioned)
INFO("[DONE] build_matrix 空 name schema 拦截")

# 清理测试 project 目录
import shutil
shutil.rmtree(PROJ_PATH, ignore_errors=True)

INFO("[DONE] test_userdoc")
