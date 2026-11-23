#pragma once

// =============================================================================
// spef db 解析边界领域结构（SPEF = Standard Parasitic Exchange Format，
// 标准寄生交换格式，IEEE 1481；语法权威口径 = spef db 立项方案
// docs/emir/spef-db-plan.md §2 + 方言与解析器容忍清单 §2.5，裁定记录 §11）。
//
// 职责边界：本头只定义「文件 → 结构」的解析产物——聚合量形态，不保留
// 逐条明细（CONN 内容不入库〔裁定 3〕、RES 只计数、R_NET 的 π 模型明细
// 不入库〔裁定 5〕）；节点原文只在解析期用于判定电容归属，不落库（裁
// 定 2）。名字保持解析边界原文（含反斜杠转义，不做去转义——去转义与
// 查找容错属换算合并层职责）；名字映射展开是解析边界的例外（引用
// *<正整数> 处当场取原名，未登记处置见裁定 11）。落库形态
//（PART_*.NET_LOAD / summary）属换算合并层（后续批次 sp_partition）。
//
// 结构：
//   SPNetKind       网段种类（*D_NET 分布式网 / *R_NET 缩减网）
//   SPFileHeader    文件头：设计名/分隔符/单位换算/名字映射表/电源地网
//                   声明 + 原声明可追溯字段（临时对象跨任务共享——
//                   文件入口任务解析、逐块解析任务消费，需序列化）
//   SPNet           一个网段的解析产物（行首自报总电容 + 对地/耦合明细
//                   合计 + 记录计数 + 多值观测）
//   SPChunkResult   一次块解析（或整文件解析）的产物：网表 + 全部统计
//                   计数（段级兜底的观测面——跳过/破损/未登记/丢弃）
//   SPFileChunk     单块切片：字节区间 [start, end)
//   SPFileChunkPlan 单文件切块计划：文件名/大小/头段终点 + 块表（temp）
//
// 单位约定：内部单位电容飞法（fF，double）、电阻欧姆（ohm，double）
//（裁定 7）——换算 = 文件数值 x 刻度 x 系数（PF -> 1e3 fF、FF -> 1；
// KOHM -> 1e3 ohm、OHM -> 1）。时间（NS/PS）与电感（HENRY/MH/UH）首版
// 不消费，只在头部记录原声明（可追溯）。
//
// 词法公共单点：数值 token 解析（sp_parse_number / sp_parse_value）、
// 缺省取值下标规则（sp_default_value_index，裁定 6）、单位词与分隔符
// 合法集合（sp_cap_unit_* / sp_res_unit_* / sp_time_unit_valid /
// sp_ind_unit_valid / sp_delimiter_char_valid / sp_bus_bracket_valid /
// sp_bus_pair_valid）在本头声明、sp_types.cpp 实现——解析器
//（sp_parser）与后续换算合并层共用，禁止双份维护。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>

#include <cstdint>

namespace fly {

// 数值 token 解析（词法公共单点）：整数/小数/科学计数法（.5 与 2.5e-01
// 形态合法）；字符集预检（拒 inf/nan/十六进制形态）+ strtod 全消费 +
// 溢出拒绝（ERANGE 且结果 ±inf）。下溢（如 1e-400）按数值 0 收录。
// 非法 = false
bool sp_parse_number(const CMString& tok, double& v);

// 多值形态解析（词法公共单点，裁定 6）：一个数值可写成 n 个值用冒号
// 连接（n >= 1），全部读入；任一部分非法 = false
bool sp_parse_value(const CMString& tok, CMVector<double>& values);

// 缺省取值下标规则（裁定 6）：n == 3（min:typ:max 形态）取下标 1
//（典型值），其它 n 取下标 0
uint32_t sp_default_value_index(uint32_t n);

// 单位词 -> 每文件单位的内部单位系数（1 文件单位 = 系数 x 刻度）：
// 电容 fF（PF -> 1e3、FF -> 1）、电阻 ohm（KOHM -> 1e3、OHM -> 1）。
// 非法单位词返回 0（负值不可能）
double sp_cap_unit_ff_per_unit(const CMString& unit_lower);
double sp_res_unit_ohm_per_unit(const CMString& unit_lower);

// 时间（NS/PS）与电感（HENRY/MH/UH）单位词合法性（首版不消费——只校验）
bool sp_time_unit_valid(const CMString& unit_lower);
bool sp_ind_unit_valid(const CMString& unit_lower);

// 层次/引脚分隔符合法字符（*DIVIDER / *DELIMITER 取值：. / | :）
bool sp_delimiter_char_valid(char c);

// 总线分隔符（*BUS_DELIMITER）：合法括号字符与括号对（[ ]、{ }、( )、< >）
bool sp_bus_bracket_valid(char c);
bool sp_bus_pair_valid(char left, char right);

// 网段种类（*D_NET 分布式网 / *R_NET 缩减网——R_NET 收录行首总电容、
// 标缩减形态，π 模型明细不入库，裁定 5）
enum class SPNetKind : uint8_t { D_NET = 0, R_NET = 1 };

// 文件头（plan §6 文件入口任务解析产物，逐块解析任务共享；临时对象，
// 需序列化）。分隔符缺省 '/' 与 ':'（头部未声明时的兜底口径）；
// cap_scale_ff_ / res_scale_ohm_ = 1 文件单位 = 多少 fF / ohm。
struct SPFileHeader {
    CMString source_file_;       // 来源文件完整路径（可追溯）
    CMString design_name_;       // *DESIGN 设计名
    CMString version_;           // *SPEF 版本串原文（大小写保留——
                                 // 小写 "ieee 1481-1999" 合法）
    char divider_ = '/';         // 层次分隔符（*DIVIDER）
    char delimiter_ = ':';       // 引脚分隔符（*DELIMITER；节点切分用）
    char bus_left_ = '[';        // 总线分隔符（*BUS_DELIMITER；仅记录）
    char bus_right_ = ']';
    double cap_scale_ff_ = 0.0;  // 1 文件电容单位 = 多少 fF（*C_UNIT）
    double res_scale_ohm_ = 0.0; // 1 文件电阻单位 = 多少 ohm（*R_UNIT）
    // 四个单位行的原声明（"刻度 单位词" 原文；时间/电感首版不消费，
    // 电容/电阻的计算系数在 cap_scale_ff_ / res_scale_ohm_——原值留档
    // 可追溯，plan §5.2 SPFileStats.c_unit_ff_per_unit 同源）
    CMString time_unit_decl_;
    CMString cap_unit_decl_;
    CMString res_unit_decl_;
    CMString ind_unit_decl_;
    CMVector<CMString> design_flow_;  // *DESIGN_FLOW 流程标记（收集不解释）
    // *NAME_MAP：索引 -> 原名（原文形态含转义，不去转义）
    CMUnorderedMap<uint32_t, CMString> name_map_;
    CMVector<CMString> power_nets_;   // *POWER_NETS 电源网名（原名，仅记录）
    CMVector<CMString> ground_nets_;  // *GROUND_NETS 地网名（原名，仅记录）
    // 文件级观测：名字映射索引重复定义数（保留首份，观测计数）
    uint64_t name_map_dup_count_ = 0;

    FLY_SERIALIZE(source_file_, design_name_, version_, divider_, delimiter_,
                  bus_left_, bus_right_, cap_scale_ff_, res_scale_ohm_,
                  time_unit_decl_, cap_unit_decl_, res_unit_decl_,
                  ind_unit_decl_, design_flow_, name_map_, power_nets_,
                  ground_nets_, name_map_dup_count_)
};

// 一个网段的解析产物（聚合量，不保留逐条明细；plan §5.2 网负载三值
// 并存的解析面）。合计口径（spef-db-plan §2.1 + 任务规格 3.5）：
//   ground_cap_ff_    = 本网段内对地电容之和；节点为「别的网的内部
//                       节点」的记录不计（另计 external_ground）；
//   coupling_cap_ff_  = 本网段内耦合电容之和（全值，系数 1）；两个节点
//                       都是别的网的内部节点的记录不计（另计
//                       external_coupling）；
//   cap_count_        = *CAP 记录条数（对地 + 耦合；丢弃与 external
//                       的口径见 SPChunkResult 注释）；
//   total_cap_reported_ff_ = 行首自报值按取值规则取一个后换算（与明细
//                       合计是否一致不在解析层判断——合并阶段做）。
struct SPNet {
    CMString name_;     // 名字映射展开后的原文
    SPNetKind kind_ = SPNetKind::D_NET;
    double total_cap_reported_ff_ = 0.0;
    double ground_cap_ff_ = 0.0;
    double coupling_cap_ff_ = 0.0;
    uint32_t value_count_ = 1;   // 本网行首值的个数（多值形态观测）
    uint64_t cap_count_ = 0;
    uint64_t coupling_cap_count_ = 0;
    uint64_t res_count_ = 0;

    FLY_SERIALIZE(name_, kind_, total_cap_reported_ff_, ground_cap_ff_,
                  coupling_cap_ff_, value_count_, cap_count_,
                  coupling_cap_count_, res_count_)
};

// 一次块解析（或整文件解析）的产物：网表 + 全部统计计数。计数口径：
//   net_count_/dnet_count_/rnet_count_  成功收录的网（nets_.size() 同径）
//   pnet_skip_count_                    *D_PNET / *R_PNET 整段跳过数（裁定 12）
//   broken_net_count_                   段级破损整网丢弃数（残缺记录/非法
//                                       数值/进入合计的值为负/未知关键字/
//                                       *END 缺失）
//   name_map_miss_count_                未登记索引引用数（网名 -> 整网跳过、
//                                       节点实例部分 -> 记录丢弃，裁定 11）
//   dropped_record_count_               因未登记索引而丢弃的记录数（不计入
//                                       任何合计）
//   multi_value_net_count_              多值网数（value_count > 1，收录网）
//   value_count_max_                    收录网的最大值个数
//   value_index_fallback_count_         显式下标越界退回缺省规则的次数
//   cap_count_/coupling_cap_count_      *CAP 记录条数 / 其中耦合条数
//                                      （与 SPNet 同口径；external 的记录
//                                       计入 cap_count_ 但不入对应合计；
//                                       丢弃记录两者都不计）
//   res_count_                          *RES 记录条数（值不保留，裁定 2）
//   external_ground_cap_count_          对地挂别的网内部节点的记录数
//   external_coupling_cap_count_        耦合两端都是别的网内部节点的记录数
//   conn_count_                         *I / *P 行数（内容不入库，裁定 3）
//   conn_pin_cap_obs_sum_ff_            *L 引脚负载电容观测和（fF）
//   unknown_direction_count_            非 I/O/B 的方向值数（不报错）
//   induc_count_                        *INDUC 记录数（忽略并计数）
//   stray_content_count_                网与网之间的杂料计数（跳过但计数——
//                                       无声丢内容不可接受）。统计范围 =
//                                       头段结束后的网段区；口径 = 连续杂
//                                       料 token 序列记 1 次（token 之间仅
//                                       空白与注释相连算同一序列——残缺片
//                                       段或孤立 token 都是一次意外，不随
//                                       token 个数膨胀；网段关键字或流尽
//                                       结束序列），注释不计（词法透明）
struct SPChunkResult {
    CMVector<SPNet> nets_;
    uint64_t net_count_ = 0;
    uint64_t dnet_count_ = 0;
    uint64_t rnet_count_ = 0;
    uint64_t pnet_skip_count_ = 0;
    uint64_t broken_net_count_ = 0;
    uint64_t name_map_miss_count_ = 0;
    uint64_t dropped_record_count_ = 0;
    uint64_t multi_value_net_count_ = 0;
    uint32_t value_count_max_ = 0;
    uint64_t value_index_fallback_count_ = 0;
    uint64_t cap_count_ = 0;
    uint64_t coupling_cap_count_ = 0;
    uint64_t res_count_ = 0;
    uint64_t external_ground_cap_count_ = 0;
    uint64_t external_coupling_cap_count_ = 0;
    uint64_t conn_count_ = 0;
    double conn_pin_cap_obs_sum_ff_ = 0.0;
    uint64_t unknown_direction_count_ = 0;
    uint64_t induc_count_ = 0;
    uint64_t stray_content_count_ = 0;

    FLY_SERIALIZE(nets_, net_count_, dnet_count_, rnet_count_,
                  pnet_skip_count_, broken_net_count_, name_map_miss_count_,
                  dropped_record_count_, multi_value_net_count_,
                  value_count_max_, value_index_fallback_count_, cap_count_,
                  coupling_cap_count_, res_count_, external_ground_cap_count_,
                  external_coupling_cap_count_, conn_count_,
                  conn_pin_cap_obs_sum_ff_, unknown_direction_count_,
                  induc_count_, stray_content_count_)
};

// 单块切片（temp）：字节区间 [start, end)。块界 = 网段关键字（行首）
// 边界——单个网段超过切块阈值不拆分（整段进一块）；相邻块首尾衔接，
// 末块终点 = 文件尾。
struct SPFileChunk {
    uint64_t start_ = 0;
    uint64_t end_ = 0;

    FLY_SERIALIZE(start_, end_)
};

// 单文件切块计划（temp；plan §6 文件入口任务 T1 口）：头段 = 文件开头
// 到首个行首网段关键字的字节偏移；块表覆盖 [header_end_, file_size_]。
// 无网段文件（纯头部）= 零块（空结果放行语义）。
struct SPFileChunkPlan {
    CMString file_name_;
    uint64_t file_size_ = 0;
    uint64_t header_end_ = 0;
    CMVector<SPFileChunk> chunks_;

    FLY_SERIALIZE(file_name_, file_size_, header_end_, chunks_)
};

}  // namespace fly
