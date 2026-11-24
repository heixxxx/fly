"""spef 模块 py 层聚合：导出 export 符号层、db/functions 公开符号；
副作用加载 message id 注册；utils/flow 不导出（dev-rules §2 六文件）。"""

from .sp_export import *  # noqa: F401,F403  # C++ 绑定符号唯一导入点
from .sp_db import *  # noqa: F401,F403
from .sp_functions import *  # noqa: F401,F403
from . import sp_register_msg  # noqa: F401  # 消息注册副作用加载
