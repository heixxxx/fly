"""Fly bootstrap — 启动时构建用户脚本命名空间（惰性加载业务模块）。

由 C++ 入口在 ``run()`` 前预执行（``import fly.bootstrap``）。

设计要点：**不在启动时 import solver**。solver import 耗 ~244ms（numpy/scipy），
若在进程启动路径上执行，全量 QA 并发时多个 master 进程同时 import 会因 CPU 争用
放大开销，挤压时序敏感测试（如 test_task_failure_cleanup 的 15s wait_for）超时余量。

因此 solver 采用**惰性加载**：
  - :class:`_LazySolverProject` 注入命名空间，首次实例化时才 import solver（此时
    agent 已就绪、worker 在连接，一次性开销不影响调度）。
  - :func:`fly.help` 首次查询 solver 相关 API 时才触发 solver 的 @document 注册。
  - 不用 solver 的脚本/进程：零 solver 开销。

fly 包初始化（``fly/__init__.py``）保持纯粹，不强依赖 solver。
"""

import fly


class _LazySolverProject:
    """``SolverProject`` 的惰性代理：首次实例化时才 import solver。

    不用 solver 的脚本完全不付 solver import（~244ms）开销；用到时才一次性加载。
    """

    __slots__ = ()

    def __new__(cls, *args, **kwargs):
        # 首次实例化：触发 solver 加载，然后用真正的 SolverProject 创建实例
        from solver import SolverProject
        return SolverProject(*args, **kwargs)

    def __getattr__(self, name):
        # 类方法/属性访问（如 SolverProject.load）也触发加载并转发
        from solver import SolverProject
        return getattr(SolverProject, name)


def _ensure_solver_help():
    """惰性触发 solver 的 @register_flow + @document 注册（填充 help registry）。

    由 help 系统在查询时调用，避免启动时加载。幂等（Python import 缓存）。
    """
    try:
        import solver  # noqa: F401
    except ImportError:
        pass


# 注册延迟加载钩子：help 首次查询时才 import solver（填充 flow 文档），避免启动开销
try:
    from fly.userdoc import register_lazy_loader
    register_lazy_loader(_ensure_solver_help)
except ImportError:
    pass


def get_script_namespace():
    """构建注入用户脚本/交互 shell 的全局命名空间（零 import 开箱即用）。

    返回的 dict 含 fly 全部公共 API（``__all__``）+ ``SolverProject``（惰性代理），
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
    # SolverProject 用惰性代理注入：不用 solver 时零开销，用到时才加载
    ns["SolverProject"] = _LazySolverProject
    return ns
