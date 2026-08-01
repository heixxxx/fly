"""E2E test: UserDoc Help 系统端到端（零 import + 完整用户路径）。

本测试验证真实用户使用路径的完整链路，**不写任何显式 import**：
  - 零 import 开箱即用：help / SolverProject / Schema / UserDoc / document 由 fly 进程
    在脚本执行前经 bootstrap 注入命名空间，脚本顶层直接可用。
  - help() 列出全部 API
  - help(keyword) 精确匹配返回详情（prototype / parameters / examples / keywords）
  - help(keyword) 模糊匹配多个时给列表提示
  - help() 无匹配时给提示
  - examples 与 keywords 在详情中完整展示
  - 嵌套 schema（dict in list）校验报错带路径

与 test_userdoc.py 的区别：后者用 ``from fly import help`` 测校验逻辑；
本测试完全不 import fly，验证 fly 进程预注入符号的端到端体验。
"""
import io
import contextlib

from _fly_log import INFO


def _capture(*args, **kwargs):
    """捕获 help(...) 的 stdout。help 符号由 bootstrap 注入到本脚本 globals。"""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help(*args, **kwargs)
    return buf.getvalue()


# ── 1. 零 import：注入的符号可直接使用 ─────────────────────────────────
# help / SolverProject / Schema / UserDoc / document 不经任何 import 即可用
# （若 bootstrap 未注入，下面访问会 NameError）
assert callable(help), "help should be injected and callable"
assert SolverProject is not None, "SolverProject should be injected"
assert callable(document), "document should be injected"
assert callable(Schema), "Schema should be injected"
INFO("[PASS] 零 import：help / SolverProject / Schema / document 可用")

# ── 2. help() 列出全部 API ──────────────────────────────────────────────
listing = _capture(None)
assert "solve" in listing, "help() should list solve"
assert "build_matrix" in listing, "help() should list build_matrix"
assert "SolverProject" in listing
INFO("[PASS] help() 列出 build_matrix / solve")

# ── 3. help('solve') 默认精简：描述 + prototype ─────────────────────────
solve_compact = _capture("solve")
assert "solve" in solve_compact
assert "Prototype" in solve_compact
assert "solve(" in solve_compact and "nsd" in solve_compact   # prototype 含参数名
# 精简模式不含参数详情 / 示例 / 关键词
assert "Parameters" not in solve_compact
assert "Examples" not in solve_compact
assert "Keywords" not in solve_compact
assert "detail=True" in solve_compact     # 提示如何看完整文档
INFO("[PASS] help('solve') 默认精简（描述 + prototype）")

# ── 3b. help('solve', detail=True) 完整文档 ─────────────────────────────
solve_full = _capture("solve", detail=True)
assert "solve" in solve_full
assert "Prototype" in solve_full
assert "Parameters" in solve_full
assert "Examples" in solve_full
assert "Keywords" in solve_full
assert "build_matrix" in solve_full or "matrix_db" in solve_full   # examples 内容
assert "ras" in solve_full or "solver" in solve_full               # keywords 展示
INFO("[PASS] help('solve', detail=True) 完整文档（parameters/examples/keywords）")

# ── 4. help('build_matrix') 默认精简 ────────────────────────────────────
bm_detail = _capture("build_matrix")
assert "build_matrix" in bm_detail
assert "matrix_path" in bm_detail     # prototype 含参数名
assert "Prototype" in bm_detail
assert "Parameters" not in bm_detail  # 精简
INFO("[PASS] help('build_matrix') 默认精简")

# ── 4b. help(all=True) 输出全部 API 完整详情 ────────────────────────────
all_details = _capture(all=True)
assert "solve" in all_details and "build_matrix" in all_details
assert all_details.count("Prototype") >= 2   # 每个 API 都有 prototype
assert "Parameters" in all_details            # all 模式展开完整详情
assert "detail=True" not in all_details       # all 不显示精简提示
INFO("[PASS] help(all=True) 输出全部 API 完整详情")

# ── 5. 无匹配时给提示 ───────────────────────────────────────────────────
no_match = _capture("nonexistent_xyz_123")
assert "No API matched" in no_match or "nonexistent_xyz_123" in no_match
INFO("[PASS] help(无匹配) 给提示")

# ── 6. help('SolverProject') 类详情（类现在也是注册 API）───────────────
# SolverProject 经 @document 注册为独立 API，help 精确匹配返回其类详情
sp_detail = _capture("SolverProject")
assert "SolverProject" in sp_detail
assert "Prototype" in sp_detail
assert "db_path" in sp_detail        # __init__ 参数
INFO("[PASS] help('SolverProject') 返回类详情")

# ── 6b. 多匹配场景：'set' 匹配 MapReduceJob 的多个 set_* 方法 ──────────
multi_match = _capture("set")
# set_partitioner / set_processor / set_merger 等多个匹配 → 列表提示
assert "Found" in multi_match or "exact_name" in multi_match
assert "MapReduceJob" in multi_match
INFO("[PASS] help('set') 多匹配给列表提示")

# ── 7. 嵌套 schema 校验报错带路径 ──────────────────────────────────────
# 构造一个临时 API 用嵌套 schema，验证报错路径
nested_doc = UserDoc("嵌套校验测试 API")
nested_doc.add_param("workers",
    schema=Schema.list(
        Schema.dict(
            required={"host": Schema(str), "port": Schema(int, check=lambda p: 1 <= p <= 65535,
                                                            error="端口范围 [1,65535], got {value}")},
        ),
    ),
    required=True)


@document(nested_doc)
def configure_workers(self, workers):
    return workers


# 触发嵌套校验失败：第二元素的 port 超范围
try:
    configure_workers(None, [{"host": "h", "port": 80}, {"host": "h2", "port": 70000}])
    assert False, "should raise ValueError"
except ValueError as e:
    msg = str(e)
    assert "[1]" in msg, f"error should carry element index path: {msg}"
    assert "port" in msg
    assert "70000" in msg or "[1,65535]" in msg
INFO("[PASS] 嵌套 schema 校验报错带路径 [1].port")

INFO("[PASS] test_userdoc_e2e")
