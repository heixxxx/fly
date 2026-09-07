"""fly 聚合包 export 层。

fly 自身无独立 .so 绑定;message 模块无独立 Python 包,其绑定
_fly_message 的导入点并入本层（lib-enhancement-plan.md §B）。message
的使用统一走 fly.* 公开包装（message / register_message_id 等）,
业务代码禁止直接消费本模块的 _msg。
"""

import _fly_message as _msg
