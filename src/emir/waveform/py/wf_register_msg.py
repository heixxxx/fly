"""waveform 模块的 message id 注册。

全局区直书（模块导入即生效，无函数包装）：__init__.py 在 export 层之后
import 本模块完成注册。仅注册过的 id 才会被 MSG 打印/发送，级别在此绑定。

- WFMG::0001: net 名未匹配（①形态跳过 + 计数，一次汇总；settings-only
  模式的 overrides 未解析条目并入本族一次透出）
- WFMG::0002: 实例/引脚名未匹配（②形态双未命中）
- WFMG::0003: 网条目无连接位/悬空（含 design db NETS 无此网——未布线
  设计合法兜底，同 TIMG::0003 口径）
- WFMG::0004: 全部输入解析成功但 0 有效条目（空结果放行，不 raise）
- WFMG::0006: 多文件实例引脚重合按文件先后顺序覆盖（场景 b/混合——后
  文件替换先文件，plan §2.1 已裁定；块绑定文件引脚集合理论不相交，重
  合即输入异常观测）
- WFMG::0007: 场景 a 时间轴观测（文件间时间重叠/回退段计数——偏移对
  齐后仍非单调的段；主语义 = 时间序衔接直和）
- WFMG::0008: 未放置实例跳过（design db 无 primary 分区副本，无归属）
- WFMG::0009: 全部文件解析失败（流程级范式 (a)：fatal message 码 80 退
  出 + master 联动——下游数据无法产出）
- WFMG::0010: strip_prefix 未命中条目跳过（含剥后余空；一次汇总）
- WFMG::0011: 绑定目标未命中（block_inst/block_cell；条目级兜底跳过该
  文件，仅全部跳过才失败——同 TIMG::0011）
- WFMG::0012: 不支持构造观测（real/event 类型 var、EVCD 扩展记录；跳
  过 + 计数）
- WFMG::0013: 向量位宽失配观测（design pin 集合与 var 位集合不一致，
  不 fatal）
"""

from fly import register_message_id

register_message_id("WFMG::0001", "WARN")
register_message_id("WFMG::0002", "WARN")
register_message_id("WFMG::0003", "WARN")
register_message_id("WFMG::0004", "WARN")
register_message_id("WFMG::0006", "WARN")
register_message_id("WFMG::0007", "WARN")
register_message_id("WFMG::0008", "WARN")
register_message_id("WFMG::0009", "FATAL")
register_message_id("WFMG::0010", "WARN")
register_message_id("WFMG::0011", "ERROR")
register_message_id("WFMG::0012", "WARN")
register_message_id("WFMG::0013", "WARN")
