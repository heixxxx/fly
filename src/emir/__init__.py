# emir 包入口：一次性聚合加载全部子模块（用户裁定：开启即可用——
# fly 启动时 import emir，flow 与消息注册随之完成）。
# 顺序：project 先（EMIRProject 类定义），各 db 子模块后（@register_flow
# 注册 + 消息注册）。随新 db 子模块在此追加。
from emir.project import *  # noqa: F401,F403
from emir.lib import *  # noqa: F401,F403
