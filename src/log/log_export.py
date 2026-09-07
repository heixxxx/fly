"""log 模块 export 层——_fly_log.so 符号唯一导入点。

log 为纯 C++ 模块（无业务 Python 源码），本文件承载全部 Python 绑定
符号的导入；包根 ``from log import INFO`` 统一经此转发。全仓库含 C++
绑定的 Python 包均遵循此 export 导入层模式（docs/emir/lib-enhancement-plan.md §B）。
"""

from _fly_log import (
    DBG,
    INFO,
    WARN,
    ERR,
    EXLogLevel,
    flush_log,
    init_log,
    set_log_level,
    shutdown_log,
)
