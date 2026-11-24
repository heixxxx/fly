"""spef 模块的 message id 注册。

全局区直书（模块导入即生效，无函数包装）：__init__.py 在 export 层之后
import 本模块完成注册。仅注册过的 id 才会被 MSG 打印/发送，级别在此绑定。

- SPMG::0001: 网名未匹配（跳过 + 计数，一次汇总；SPEF 与 design 网表
  失配的兜底观测）
- SPMG::0002: strip_prefix 未命中条目跳过（含剥后余空；同 TIMG::0010
  口径）
- SPMG::0003: pg 网跳过（电源地网全局集命中——寄生无功耗语义，防御性
  口径）
- SPMG::0004: 全部输入解析成功但 0 有效网（空结果放行，不 raise）
- SPMG::0005: R_NET 缩减网形态收录观测（行首总电容照常收录，π 模型明
  细不入库）
- SPMG::0006: 多值形态观测（按取值规则取其一——裁定 6）与取值下标回
  退（显式下标越界退回缺省规则）
- SPMG::0007: 同作用域的跨文件同网重合（保留首份——裁定 8 + 计数）
- SPMG::0008: 全部文件解析失败（流程级范式 (a)：fatal message 码 80 退
  出 + master 联动——下游数据无法产出）
- SPMG::0009: 绑定目标未命中（block_inst/block_cell；条目级兜底跳过该
  文件，仅全部跳过才失败——同 WFMG::0011）
- SPMG::0010: 段级语法破损跳过（残缺记录/非法数值/进入合计的值为负/
  未知关键字/*END 缺失）
- SPMG::0011: 网名命中但 design db 无布线几何或无可挂连接位（未布线设
  计的合法兜底，同 WFMG::0003 / TIMG::0003 口径）：跳过 + 计数，下游该
  网视为未覆盖
- SPMG::0012: 名字映射表里引用了未登记的索引（整网跳过或记录丢弃——
  裁定 11）
- SPMG::0013: 行首总电容与明细合计不一致（超过容差的网数与最大相对偏
  差——只观测，不改数值）
- SPMG::0014: 物理网段（*D_PNET / *R_PNET）跳过 + 计数（裁定 12）
"""

from fly import register_message_id

register_message_id("SPMG::0001", "WARN")
register_message_id("SPMG::0002", "WARN")
register_message_id("SPMG::0003", "WARN")
register_message_id("SPMG::0004", "WARN")
register_message_id("SPMG::0005", "WARN")
register_message_id("SPMG::0006", "WARN")
register_message_id("SPMG::0007", "WARN")
register_message_id("SPMG::0008", "FATAL")
register_message_id("SPMG::0009", "ERROR")
register_message_id("SPMG::0010", "WARN")
register_message_id("SPMG::0011", "WARN")
register_message_id("SPMG::0012", "WARN")
register_message_id("SPMG::0013", "WARN")
register_message_id("SPMG::0014", "WARN")
