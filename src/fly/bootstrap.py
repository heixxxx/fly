"""Fly bootstrap — 启动时预加载模块 + 构建用户脚本命名空间。

由 C++ 入口在 ``run()`` 前预执行（``import fly.bootstrap``），集中管理"开箱即用"
所需的模块预 import，使 help 系统在用户脚本执行前就填充完毕。

职责：
  1. 预 import fly 核心包 + 业务模块（solver 等），触发其 ``@document`` 装饰器
     执行，把 flow 注册进 :data:`fly.userdoc._HELP_REGISTRY`。
  2. 提供 :func:`get_script_namespace` 返回注入用户脚本/交互 shell 的符号表，
     使用户脚本零 import 即可直接 ``help()``、``SolverProject()``、``open_db()``。

fly 包初始化（``fly/__init__.py``）保持纯粹，不强依赖 solver；是否预加载 solver
由本模块决定（bootstrap 时预加载，普通 ``import fly`` 不加载）。
"""

import fly

# 预加载业务模块：触发 @register_flow + @document 注册，填充 help registry。
# 放在 import fly 之后（solver.flows 会 from fly import register_flow 等，
# 此时 fly 已完全初始化，无循环导入风险）。
try:
    import solver
    from solver import SolverProject
except ImportError:
    solver = None


def get_script_namespace():
    """构建注入用户脚本/交互 shell 的全局命名空间（零 import 开箱即用）。

    返回的 dict 含 fly 全部公共 API（``__all__``）+ ``SolverProject``，
    供 :func:`fly.main._run_master` 的 ``exec`` 与 ``code.interact`` 使用。
    """
    ns = {"__name__": "__main__"}
    # 注入 fly.__all__ 中的全部公共符号
    for name in getattr(fly, "__all__", []):
        try:
            ns[name] = getattr(fly, name)
        except AttributeError:
            pass
    # help 优先用 fly 的（覆盖 builtins.help，fly.help 更实用）
    ns["help"] = fly.help
    # SolverProject（若 solver 预加载成功）
    if solver is not None:
        ns["SolverProject"] = solver.SolverProject
    return ns
