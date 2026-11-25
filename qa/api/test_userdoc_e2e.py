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

from test import record


def _capture(*args, **kwargs):
    """捕获 help(...) 的 stdout。help 符号由 bootstrap 注入到本脚本 globals。"""
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        help(*args, **kwargs)
    return buf.getvalue()


# ── 1. 零 import：注入的符号可直接使用 ─────────────────────────────────
# help / SolverProject / Schema / UserDoc / document 不经任何 import 即可用
# （若 bootstrap 未注入，下面访问会 NameError）
help_injected = callable(help)
# 原断言：assert callable(help), "help should be injected and callable"
record("injected/help_callable", help_injected)
sp_injected = SolverProject is not None
# 原断言：assert SolverProject is not None, "SolverProject should be injected"
record("injected/solver_project_present", sp_injected)
document_injected = callable(document)
# 原断言：assert callable(document), "document should be injected"
record("injected/document_callable", document_injected)
schema_injected = callable(Schema)
# 原断言：assert callable(Schema), "Schema should be injected"
record("injected/schema_callable", schema_injected)
INFO("[DONE] 零 import：help / SolverProject / Schema / document 可用")

# ── 2. help() 列出全部 API ──────────────────────────────────────────────
listing = _capture(None)
lists_solve = "solve" in listing
# 原断言：assert "solve" in listing, "help() should list solve"
record("help_listing/mentions_solve", lists_solve, detail=listing)
lists_build_matrix = "build_matrix" in listing
# 原断言：assert "build_matrix" in listing, "help() should list build_matrix"
record("help_listing/mentions_build_matrix", lists_build_matrix)
lists_solver_project = "SolverProject" in listing
# 原断言：assert "SolverProject" in listing
record("help_listing/mentions_solver_project", lists_solver_project)
INFO("[DONE] help() 列出 build_matrix / solve")

# ── 3. help('solve') 默认精简：描述 + prototype ─────────────────────────
solve_compact = _capture("solve")
compact_mentions_solve = "solve" in solve_compact
# 原断言：assert "solve" in solve_compact
record("solve_compact/mentions_solve", compact_mentions_solve, detail=solve_compact)
compact_mentions_prototype = "Prototype" in solve_compact
# 原断言：assert "Prototype" in solve_compact
record("solve_compact/mentions_prototype", compact_mentions_prototype)
compact_has_params = "solve(" in solve_compact and "nsd" in solve_compact   # prototype 含参数名
# 原断言：assert "solve(" in solve_compact and "nsd" in solve_compact
record("solve_compact/prototype_has_params", compact_has_params)
# 精简模式不含参数详情 / 示例 / 关键词
compact_no_parameters = "Parameters" not in solve_compact
# 原断言：assert "Parameters" not in solve_compact
record("solve_compact/no_parameters_section", compact_no_parameters)
compact_no_examples = "Examples" not in solve_compact
# 原断言：assert "Examples" not in solve_compact
record("solve_compact/no_examples_section", compact_no_examples)
compact_no_keywords = "Keywords" not in solve_compact
# 原断言：assert "Keywords" not in solve_compact
record("solve_compact/no_keywords_section", compact_no_keywords)
compact_hints_detail = "detail=True" in solve_compact     # 提示如何看完整文档
# 原断言：assert "detail=True" in solve_compact
record("solve_compact/hints_detail_flag", compact_hints_detail)
INFO("[DONE] help('solve') 默认精简（描述 + prototype）")

# ── 3b. help('solve', detail=True) 完整文档 ─────────────────────────────
solve_full = _capture("solve", detail=True)
full_mentions_solve = "solve" in solve_full
# 原断言：assert "solve" in solve_full
record("solve_full/mentions_solve", full_mentions_solve, detail=solve_full)
full_mentions_prototype = "Prototype" in solve_full
# 原断言：assert "Prototype" in solve_full
record("solve_full/mentions_prototype", full_mentions_prototype)
full_mentions_parameters = "Parameters" in solve_full
# 原断言：assert "Parameters" in solve_full
record("solve_full/mentions_parameters", full_mentions_parameters)
full_mentions_examples = "Examples" in solve_full
# 原断言：assert "Examples" in solve_full
record("solve_full/mentions_examples", full_mentions_examples)
full_mentions_keywords = "Keywords" in solve_full
# 原断言：assert "Keywords" in solve_full
record("solve_full/mentions_keywords", full_mentions_keywords)
full_examples_content = "build_matrix" in solve_full or "matrix_db" in solve_full   # examples 内容
# 原断言：assert "build_matrix" in solve_full or "matrix_db" in solve_full
record("solve_full/examples_content_present", full_examples_content)
full_keywords_content = "ras" in solve_full or "solver" in solve_full               # keywords 展示
# 原断言：assert "ras" in solve_full or "solver" in solve_full
record("solve_full/keywords_content_present", full_keywords_content)
INFO("[DONE] help('solve', detail=True) 完整文档（parameters/examples/keywords）")

# ── 4. help('build_matrix') 默认精简 ────────────────────────────────────
bm_detail = _capture("build_matrix")
bm_mentions_self = "build_matrix" in bm_detail
# 原断言：assert "build_matrix" in bm_detail
record("build_matrix_help/mentions_build_matrix", bm_mentions_self, detail=bm_detail)
bm_mentions_matrix_path = "matrix_path" in bm_detail     # prototype 含参数名
# 原断言：assert "matrix_path" in bm_detail
record("build_matrix_help/prototype_has_matrix_path", bm_mentions_matrix_path)
bm_mentions_prototype = "Prototype" in bm_detail
# 原断言：assert "Prototype" in bm_detail
record("build_matrix_help/mentions_prototype", bm_mentions_prototype)
bm_compact = "Parameters" not in bm_detail  # 精简
# 原断言：assert "Parameters" not in bm_detail
record("build_matrix_help/no_parameters_section", bm_compact)
INFO("[DONE] help('build_matrix') 默认精简")

# ── 4b. help(all=True) 输出全部 API 完整详情 ────────────────────────────
all_details = _capture(all=True)
all_both_listed = "solve" in all_details and "build_matrix" in all_details
# 原断言：assert "solve" in all_details and "build_matrix" in all_details
record("help_all_detail/lists_solve_and_build_matrix", all_both_listed, detail=all_details)
all_prototype_count = all_details.count("Prototype") >= 2   # 每个 API 都有 prototype
# 原断言：assert all_details.count("Prototype") >= 2
record("help_all_detail/prototypes_at_least_2", all_prototype_count,
       detail=all_details.count("Prototype"))
all_full_params = "Parameters" in all_details            # all 模式展开完整详情
# 原断言：assert "Parameters" in all_details
record("help_all_detail/mentions_parameters", all_full_params)
all_no_compact_hint = "detail=True" not in all_details       # all 不显示精简提示
# 原断言：assert "detail=True" not in all_details
record("help_all_detail/no_compact_hint", all_no_compact_hint)
INFO("[DONE] help(all=True) 输出全部 API 完整详情")

# ── 5. 无匹配时给提示 ───────────────────────────────────────────────────
no_match = _capture("nonexistent_xyz_123")
no_match_hinted = "No API matched" in no_match or "nonexistent_xyz_123" in no_match
# 原断言：assert "No API matched" in no_match or "nonexistent_xyz_123" in no_match
record("help_no_match/hints", no_match_hinted, detail=no_match)
INFO("[DONE] help(无匹配) 给提示")

# ── 6. help('SolverProject') 类详情（类现在也是注册 API）───────────────
# SolverProject 经 @document 注册为独立 API，help 精确匹配返回其类详情
sp_detail = _capture("SolverProject")
sp_mentions_self = "SolverProject" in sp_detail
# 原断言：assert "SolverProject" in sp_detail
record("solver_project_help/mentions_solver_project", sp_mentions_self, detail=sp_detail)
sp_mentions_prototype = "Prototype" in sp_detail
# 原断言：assert "Prototype" in sp_detail
record("solver_project_help/mentions_prototype", sp_mentions_prototype)
sp_mentions_db_path = "db_path" in sp_detail        # __init__ 参数
# 原断言：assert "db_path" in sp_detail
record("solver_project_help/mentions_db_path", sp_mentions_db_path)
INFO("[DONE] help('SolverProject') 返回类详情")

# ── 6b. 多匹配场景：'set' 匹配 MapReduceJob 的多个 set_* 方法 ──────────
multi_match = _capture("set")
# set_partitioner / set_processor / set_merger 等多个匹配 → 列表提示
multi_list_hinted = "Found" in multi_match or "exact_name" in multi_match
# 原断言：assert "Found" in multi_match or "exact_name" in multi_match
record("help_multi_match/list_hinted", multi_list_hinted, detail=multi_match)
multi_mentions_mrj = "MapReduceJob" in multi_match
# 原断言：assert "MapReduceJob" in multi_match
record("help_multi_match/mentions_mapreducejob", multi_mentions_mrj)
INFO("[DONE] help('set') 多匹配给列表提示")

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
nested_type = None
nested_has_path = False
nested_mentions_port = False
nested_mentions_range = False
try:
    configure_workers(None, [{"host": "h", "port": 80}, {"host": "h2", "port": 70000}])
    # 保留：没抛异常时的立即失败（否定性检查），不依赖记录
    assert False, "should raise ValueError"
except ValueError as e:
    msg = str(e)
    nested_type = type(e).__name__
    nested_has_path = "[1]" in msg
    nested_mentions_port = "port" in msg
    nested_mentions_range = "70000" in msg or "[1,65535]" in msg
# 原断言：assert "[1]" in msg, f"error should carry element index path: {msg}"
record("nested_schema/error_type", nested_type)
record("nested_schema/mentions_element_path", nested_has_path, detail=msg if nested_type else None)
# 原断言：assert "port" in msg
record("nested_schema/mentions_port", nested_mentions_port)
# 原断言：assert "70000" in msg or "[1,65535]" in msg
record("nested_schema/mentions_range", nested_mentions_range)
INFO("[DONE] 嵌套 schema 校验报错带路径 [1].port")

INFO("[DONE] test_userdoc_e2e")
