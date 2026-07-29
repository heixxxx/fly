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
assert "solve" in solve_help, "help('solve') should mention solve"
assert "nsd" in solve_help, "help('solve') should list nsd param"
assert "Prototype" in solve_help, "help('solve') should show prototype"
assert "SolverProject" in solve_help, "help('solve') should show owner"
INFO("[PASS] help('solve') 输出含 prototype / nsd / SolverProject")

# help() 列出全部
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    help()
all_help = buf.getvalue()
assert "solve" in all_help
assert "build_matrix" in all_help
INFO("[PASS] help() 列出 build_matrix / solve")


# ── 参数校验：solve 非法参数聚合报错 ────────────────────────────────────

PROJ_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "test_userdoc_proj")
proj = SolverProject(PROJ_PATH)

# 场景 1：多参数同时非法 → 聚合错误
try:
    proj.solve(name="s", matrix_db=None, nsd=0, overlap_ratio=2.0)
    assert False, "solve with invalid args should raise ValueError"
except ValueError as e:
    msg = str(e)
    # matrix_db None → 类型错误（expected _Database）
    assert "matrix_db" in msg, f"should report matrix_db: {msg}"
    # nsd=0 → check 错误
    assert "nsd" in msg and ">= 1" in msg, f"should report nsd: {msg}"
    # overlap_ratio=2.0 → check 错误
    assert "overlap_ratio" in msg, f"should report overlap_ratio: {msg}"
INFO("[PASS] solve 多参数非法聚合报错")

# 场景 2：omega 非法字符串 → any_of 两分支都失败
try:
    proj.solve(name="s", matrix_db=None, nsd=4, omega="bad")
    assert False
except ValueError as e:
    msg = str(e)
    assert "omega" in msg
    assert "coarse" in msg or "adaptive" in msg, f"should hint valid omega: {msg}"
INFO("[PASS] solve omega 非法 any_of 报错")

# 场景 3：build_matrix 空 name → schema 拦截
try:
    proj.build_matrix(name="", matrix_path="x.npz")
    assert False
except ValueError as e:
    assert "name" in str(e)
INFO("[PASS] build_matrix 空 name schema 拦截")

# 清理测试 project 目录
import shutil
shutil.rmtree(PROJ_PATH, ignore_errors=True)

INFO("[PASS] test_userdoc")
