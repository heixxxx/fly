"""design 模块的 message id 注册。

全局区直书（模块导入即生效，无函数包装）：__init__.py 在 export 层之后
import 本模块完成注册。仅注册过的 id 才会被 MSG 打印/发送，级别在此绑定。

- DSGN::0001: 跨文件重复 macro/block cell（保留首份抛弃后续 + 两处来源）
- DSGN::0002: lef cell 有 / lib cell 无（EMIR 无电流模型，名单汇总）
- DSGN::0003: lib cell 有 / lef cell 无（未被覆盖，名单汇总）
- DSGN::0004: cell 的 pin 集合 lib/lef 不一致（逐 cell 缺失 pin 名单）
- DSGN::0005: via cell 重名冲突（保留首份 + 登记名）
- DSGN::0006: DEF 内 PINS 段 port 名重复（保留首份）/ DIEAREA 缺失兜底
- DSGN::0007: COMPONENTS 引用未定义 cell（fake cell 兜底创建，⑲/⑳，
  不 raise 不跳过）
- DSGN::0008: 网内容解析引用未定义 via cell（跳过该 via instance +
  计数提醒，不 raise 不拦截）
- DSGN::0009: 网内容解析统计汇总（网数/连接/几何/via instance 数）
"""

from fly import register_message_id

register_message_id("DSGN::0001", "WARN")
register_message_id("DSGN::0002", "WARN")
register_message_id("DSGN::0003", "WARN")
register_message_id("DSGN::0004", "WARN")
register_message_id("DSGN::0005", "WARN")
register_message_id("DSGN::0006", "WARN")
register_message_id("DSGN::0007", "WARN")
register_message_id("DSGN::0008", "WARN")
register_message_id("DSGN::0009", "INFO")
