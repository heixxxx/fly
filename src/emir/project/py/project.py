"""EMIRProject — EMIR（电压降 IR + 电迁移 EM）仿真分析的 Project 类型。

全部 13 个数据库（lib / tech / design / extraction / spef / matrix / timing /
vcd / switching / power / current / analysis / em）及其创建流程 API 均注册
归属本 Project 子类（归属约定与 API 规划表见 docs/emir-data-flow.md §5）。

文件组织（体现"分离到不同模块"，详见 docs/project-design.md §4.1）::

    emir/project/py/project.py   ← 本文件：EMIRProject 类定义（近乎为空）
    emir/<db>/py/flow.py         ← 各建库 flow 的 @register_flow 实现（随立项逐个建立）
    emir/<db>/py/db.py           ← 各 db 子类（role 体系，随立项逐个建立）

典型用法（首个 flow 立项后）::

    from emir import EMIRProject

    proj = EMIRProject("./emir_run")
    lib_db = proj.build_lib_db(name="lib", lib_paths=["nangate45.lib"])
    proj.wait_frozen("lib", timeout=3600)
"""

from fly.project import Project
from fly import UserDoc, Schema, document


# EMIRProject 的 UserDoc：校验 __init__(db_path) + help 文档
emir_project_doc = UserDoc(
    "EMIR（电压降 IR + 电迁移 EM）仿真分析 Project，"
    "管理 13 个数据库的创建流程（规划见 docs/emir-data-flow.md）。")
emir_project_doc.add_param("db_path",
    schema=Schema(str, check=lambda s: len(s) > 0, error="must not be empty"),
    required=True, desc="project 目录路径（不存在则创建）")
emir_project_doc.add_example("基础用法",
    code='''proj = EMIRProject("./emir_run")
# 各数据库的创建 flow API 随立项逐步注册（归属表见 docs/emir-data-flow.md §5）：
# lib_db = proj.build_lib_db(name="lib", lib_paths=["nangate45.lib"])
# proj.wait_frozen("lib", timeout=3600)''',
    desc="构造 project → 建库 flow（随立项可用）→ 等待冻结")
emir_project_doc.add_keyword(["emir", "ir", "em", "project"])


@document(emir_project_doc)
class EMIRProject(Project):
    """EMIR 仿真分析的 Project 类型。

    业务流程（13 个数据库的建库 API，归属表见 docs/emir-data-flow.md §5）
    在各 db 子模块的 ``flow.py`` 中通过 ``@register_flow(EMIRProject)`` 注册，
    本类体仅承载类身份（用于 load_project 时按 meta["class"] 动态还原）。
    """

    pass


# 尾部 import 各 db 子包：触发 @register_flow(EMIRProject) 注册（此时
# EMIRProject 已定义，与 solver/project.py 尾部 import flows 同构）。
# 随新 db 立项在此追加。
from emir.lib import *  # noqa: E402,F401,F403
