"""Fly bootstrap — 启动时构建用户脚本命名空间（惰性加载业务模块）。

由 C++ 入口在 ``run()`` 前预执行（``import fly.bootstrap``）。

设计要点：**不在启动时 import 业务模块**。重业务模块（如 solver）import 耗 ~244ms
（numpy/scipy），若在启动路径执行，全量 QA 并发时多 master 同时 import 会因 CPU
争用放大开销，挤压时序敏感测试超时余量。

因此业务模块采用**惰性加载**：
  - :data:`_LAZY_MODULES` 声明表列出所有需惰性注入的模块入口（symbol → "module:attr"）。
  - :class:`_LazyAttr` 通用代理注入用户命名空间，首次实例化/属性访问时才 import。
  - help 查询时通过延迟钩子触发模块 import（填充其 ``@document`` 注册）。

**接入新业务模块的唯一改动**：在 :data:`_LAZY_MODULES` 加一行，例如::

    _LAZY_MODULES = {
        "SolverProject": "solver:SolverProject",
        "Pipeline": "pipeline:Pipeline",   # ← 新模块，仅此一行
    }

无需写代理类、无需写 help 钩子、无需改 get_script_namespace。fly 包初始化
（``fly/__init__.py``）保持纯粹，不强依赖任何业务模块。
"""

import importlib

import fly


# ── 业务模块惰性加载声明表 ──────────────────────────────────────────────
# 新增业务模块入口只需在此加一行："用户可见符号名": "模块:属性"。
# 启动时不 import 这些模块；首次访问符号（实例化 / help 查询）时才加载。
_LAZY_MODULES = {
    "SolverProject": "solver:SolverProject",
}


class _LazyAttr:
    """通用惰性代理：按 ``"module:attr"`` 目标在首次使用时加载真正的对象。

    - 作为命名空间里的 ``SolverProject``：``SolverProject(...)`` 调用 ``__call__``
      实例化真正的类；``SolverProject.load`` 类属性访问走 ``__getattr__`` 转发。
    - 不用该模块的脚本：代理从未被触发，零 import 开销。
    """

    __slots__ = ("_target",)

    def __init__(self, target):
        object.__setattr__(self, "_target", target)

    def _resolve(self):
        """import 目标模块并返回真正的属性（类/函数）。"""
        mod_name, _, attr = self._target.partition(":")
        mod = importlib.import_module(mod_name)
        return getattr(mod, attr)

    def __call__(self, *args, **kwargs):
        """SolverProject(...) → 实例化真正的类。"""
        return self._resolve()(*args, **kwargs)

    def __getattr__(self, name):
        """SolverProject.load（类属性/方法访问）→ 转发到真正的类。"""
        if name.startswith("_"):
            raise AttributeError(name)
        return getattr(self._resolve(), name)


def _register_lazy_modules():
    """把 _LAZY_MODULES 声明表灌进 userdoc 的注册表 + help 延迟钩子。

    在 bootstrap import 时调用一次（不 import 业务模块本身，仅登记）。
    """
    try:
        from fly.userdoc import register_module
    except ImportError:
        return
    for name, target in _LAZY_MODULES.items():
        register_module(name, target)


_register_lazy_modules()


def get_script_namespace():
    """构建注入用户脚本/交互 shell 的全局命名空间（零 import 开箱即用）。

    返回的 dict 含 fly 全部公共 API（``__all__``）+ 所有已注册业务模块入口
    （惰性代理），供 :func:`fly.main._run_master` 的 ``exec`` 与 ``code.interact`` 使用。
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
    # 自动注入所有已注册业务模块的惰性代理（取代逐模块硬编码）
    try:
        from fly.userdoc import _REGISTERED_MODULES
        for name, target in _REGISTERED_MODULES.items():
            ns[name] = _LazyAttr(target)
    except ImportError:
        pass
    return ns
