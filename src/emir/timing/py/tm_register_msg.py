"""timing 模块的 message id 注册。

全局区直书（模块导入即生效，无函数包装）：__init__.py 在 export 层之后
import 本模块完成注册。仅注册过的 id 才会被 MSG 打印/发送，级别在此绑定。

- TIMG::0001: 实例名/网名未匹配（跳过 + 计数，一次汇总——TWF 条目的实例
  路径未命中 design db 或网名未命中；网名未命中无专属码，并入本族文案）
- TIMG::0002: 引脚名未匹配（跳过 + 计数——全局 pin 名字空间未命中或条目
  名形态错误）
- TIMG::0003: 网条目无驱动/悬空（跳过 + 计数——分区 NETS 对象无该网记录
  或连接条目中无 driver 位条目，含仅端口网）
- TIMG::0004: 全部输入解析成功但 0 有效条目（空结果放行，不 raise）
- TIMG::0006: 跨文件同名条目冲突（保留首份；同文件跨块的同源形态不计）
- TIMG::0007: 时钟名跨文件周期/沿不一致（保留首份——顶层文件定义优先）
- TIMG::0008: 未放置实例跳过（design db 无 primary 分区副本，无归属）
- TIMG::0009: 全部文件解析失败（流程级范式 (a)：fatal message 码 80 退出
  + master 联动——下游数据无法产出）
- TIMG::0010: strip_prefix 未命中条目跳过（含剥后余空；一次汇总）
- TIMG::0011: 绑定目标未命中（block_inst 块实例路径 / block_cell 块
  cell 名不在 design db——2026-09-17 条目级兜底裁定：该文件跳过不进
  切块链、零条目入库，计数入 summary.invalid_binding_count；仅全部
  文件被跳过才任务失败）

（历史：TIMG::0005 曾用于 timing alpha 设置问题的一次汇总提醒；2026-09-17
裁定入口参数校验改为 header schema 直接 raise 后，该码再无使用点，
注册删除。）
"""

from fly import register_message_id

register_message_id("TIMG::0001", "WARN")
register_message_id("TIMG::0002", "WARN")
register_message_id("TIMG::0003", "WARN")
register_message_id("TIMG::0004", "WARN")
register_message_id("TIMG::0006", "WARN")
register_message_id("TIMG::0007", "WARN")
register_message_id("TIMG::0008", "WARN")
register_message_id("TIMG::0009", "FATAL")
register_message_id("TIMG::0010", "WARN")
register_message_id("TIMG::0011", "ERROR")
