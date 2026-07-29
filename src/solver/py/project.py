"""SolverProject — RAS solver 的 Project 模板示例。

把现有 RAS solver 包装成两个流程 API（build_matrix / solve），作为用户二次开发
Project 子类的参考。业务流程实现在 :mod:`solver.flows`，通过
``@register_flow`` 注册到本类的 ``SolverProject``。

文件组织（体现"分离到不同模块"，详见 docs/project-design.md §4.1）::

    solver/project.py   ← 本文件：SolverProject 类定义（近乎为空）
    solver/flows.py     ← build_matrix / solve 的 @register_flow 实现

典型用法::

    import fly
    from solver import SolverProject

    proj = SolverProject("./my_project")
    matrix_db = proj.build_matrix(name="matrix", matrix_path="poisson_n20.npz")
    result_db = proj.solve(name="solve", matrix_db=matrix_db, nsd=4)
"""

from fly.project import Project


class SolverProject(Project):
    """RAS solver 的 Project 模板。

    业务流程（build_matrix / solve）在 :mod:`solver.flows` 中通过
    ``@register_flow(SolverProject)`` 注册，本类体仅承载类身份（用于
    load_project 时按 meta["class"] 动态还原）。
    """

    pass


# 必须在 SolverProject 类定义之后 import flows，使 @register_flow 执行时
# SolverProject 已存在。import solver.flows 触发 flow 注册。
import solver.flows  # noqa: E402,F401
