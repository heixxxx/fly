from .task_io import *  # noqa: F401,F403
from .serve import *  # noqa: F401,F403
from .serve import main as serve_main  # noqa: F401 — CLI 入口别名（serve 模块的
# main 函数；`from monitor import serve` 解析到的是 serve() 函数，模块属性
# 被星导出覆盖，故显式起别名供 fly --serve-monitor 分支调用）
