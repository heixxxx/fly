"""lib 模块的 message id 注册。

全局区直书（模块导入即生效，无函数包装）：__init__.py 在 export 层之后
import 本模块完成注册。仅注册过的 id 才会被 MSG 打印/发送，级别在此绑定。

- LIBR::0001: merge 抛弃重复 cell（库版本混用迹象，保留首份）
- LIBR::0002: 解析成功但 0 cell（空库兜底，返回空容器）
- LIBR::0003: 部分 lib 文件解析失败（兜底跳过该文件 + 失败清单，其余文件
  照常产出 LIBLibrary——流程错误处理范式 2026-09-13，dev-rules §7.2）
- LIBR::0004: 全部 lib 文件解析失败（下游无法产出正确数据，fatal message
  结束整个 run——码 80 退出 + master 联动）
"""

from fly import register_message_id

register_message_id("LIBR::0001", "WARN")
register_message_id("LIBR::0002", "WARN")
register_message_id("LIBR::0003", "ERROR")
register_message_id("LIBR::0004", "FATAL")
