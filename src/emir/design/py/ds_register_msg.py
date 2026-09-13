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
- DSGN::0010: 层引用未定义（层名不在 stack 层表）：条目级丢弃（rect/
  via/wire/obs 等按条目类型）+ skipped_layer_ref_count 计数，不 raise
  （dev-rules §7 兜底）
- DSGN::0011: 层级树构建失败（多根/零根、nets/blocks 不对齐、环）——
  不可恢复结构错误，fatal message（进程码 80 退出 + master 联动，
  dev-rules §7 第三类处置）
- DSGN::0012: name hasher 权威段损坏（反序列化重建后计数/秩域仍不符）——
  不可恢复数据错误，fatal message（同 DSGN::0011）
- DSGN::0013: design alpha 设置问题（2026-09-13 裁定语义扩展：声明式
  validator 非法值 + 未知键，build_design_db 接线处一次汇总提醒后回退
  默认/忽略；S8 侧 '{x}x{y}' 解析失败 / partition_target_density 非正值
  仍由 C++ ds_decide_partitions 逐处提醒回退）——均不 raise（dev-rules
  §7）
- DSGN::0014: 部分 cell lef 文件解析失败（兜底跳过该文件 + 失败清单，
  空产物照常汇总、cell 缺失由 fake cell 承接——流程错误处理范式
  2026-09-13，dev-rules §7.2）
- DSGN::0015: 全部 cell lef 文件解析失败（下游无法产出正确数据，fatal
  message 结束整个 run——与 lib 全败同口径）
- DSGN::0016: DEF 文件语法/格式错误（design db 数据不完整无意义，fatal
  message 结束整个 run；source 区分触发阶段 0=S4 头扫描 1=S5a 实例
  2=S5b 网内容）
- DSGN::0017: tech lef 文件语法/格式错误（层表来源损坏无法兜底，fatal
  message 结束整个 run）
- DSGN::0018: S7 悬空 port 网（连接表含 ("PIN", port) 引用但未连接任何
  父网——照常入表 root = 自身 + 计数提醒，2026-09-13 裁定 ④；不 raise）
- DSGN::0019: S10 汇总校验损坏类——并查集不自洽（root_of_ 两层不变式
  破坏 / members_of_ 与 root_of_ 双向不一致 = union 结构错误，数据损坏；
  fatal message 码 80 退出 + master 联动，阻断损坏库冻结）
- DSGN::0020: S10 汇总校验损坏类——分区网格未无缝覆盖（分区表损坏：
  网格缺格/重复/行列边界不一致/与全局密度格网覆盖域不对齐；fatal 同上）
- DSGN::0021: S10 汇总校验损坏类——namemap 双向不一致（cell/pin/via
  cell/layer 四全局 hasher + 每伴生 instance/net hasher 全查闭环断裂 =
  映射损坏；fatal 同上）
- DSGN::0022: S10 汇总校验观测类——global id 连续性（instance/net/via
  三域的空洞/重复计数；空洞含 UNPLACED 实例/空网/root 自身等合法形态，
  可能丢数据但业务数据本身没问题；user warn，不阻断冻结）
- DSGN::0023: S10 汇总校验观测类——密度守恒 primary 口径偏差（Σ 各分区
  primary 实例计数 ≠ Σ 首份定义 (实例数 − UNPLACED)；仅影响分区结果；
  user warn，不阻断冻结）
- DSGN::0024: S10 全局统计汇总（分区数/实例 primary 与副本/网/连接/图形
  条目/跨分区网/密度三通道总量；INFO）
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
register_message_id("DSGN::0010", "WARN")
register_message_id("DSGN::0011", "FATAL")
register_message_id("DSGN::0012", "FATAL")
register_message_id("DSGN::0013", "WARN")
register_message_id("DSGN::0014", "ERROR")
register_message_id("DSGN::0015", "FATAL")
register_message_id("DSGN::0016", "FATAL")
register_message_id("DSGN::0017", "FATAL")
register_message_id("DSGN::0018", "WARN")
register_message_id("DSGN::0019", "FATAL")
register_message_id("DSGN::0020", "FATAL")
register_message_id("DSGN::0021", "FATAL")
register_message_id("DSGN::0022", "WARN")
register_message_id("DSGN::0023", "WARN")
register_message_id("DSGN::0024", "INFO")
