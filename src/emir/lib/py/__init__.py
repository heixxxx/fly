"""emir lib 库 db 的 Python 包（七文件规范，docs/emir/dev-rules.md）。

加载顺序（有依赖）：export 层最先（.so 符号就位）→ 消息注册（副作用）→
db（容器 + flow 入口）→ functions（对外读取函数）。utils/flow 为内部
装配，不经包根导出。
"""

# .so 符号唯一导入点（lib 模块内其他文件一律从这里取符号）
from .lib_export import (  # noqa: F401
    EXLIBCell,
    EXLIBHeaderAttr,
    EXLIBInternalPower,
    EXLIBLibrary,
    EXLIBPin,
    EXLIBTimingArc,
    lib_parse_lib_file,
)

# 消息 id 注册（全局区直书，导入即生效）
from . import lib_register_msg  # noqa: F401

# alpha 设置（声明式五要素，2026-09-13 裁定；对象名 "alpha_settings" 随
# 建库写入 db）
from .alpha_settings import LIBAlphaSettings, get_default_alpha_settings

# 容器 + flow 入口 + 对外函数
from .lib_db import LibDb, build_lib_db
from .lib_functions import load_lib_library
