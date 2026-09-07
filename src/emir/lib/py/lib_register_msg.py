"""lib 模块的 message id 注册。

全局区直书（模块导入即生效，无函数包装）：__init__.py 在 export 层之后
import 本模块完成注册。仅注册过的 id 才会被 MSG 打印/发送，级别在此绑定。

- LIBR::0001: merge 抛弃重复 cell（库版本混用迹象，保留首份）
- LIBR::0002: 解析成功但 0 cell（空库兜底，返回空容器）
"""

from fly import register_message_id

register_message_id("LIBR::0001", "WARN")
register_message_id("LIBR::0002", "WARN")
