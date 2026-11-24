#pragma once

// SPEF（Standard Parasitic Exchange Format，标准寄生交换格式，
// IEEE 1481）解析器：按 dev-rules §4 自研 C++ 解析器（waveform/timing
// 先例同构）。语法权威口径 = spef db 立项方案 docs/emir/spef-db-plan.md
// §2（文件结构/单位/名字映射/网段形态）+ §2.5 方言与解析器容忍清单
//（编号可省略、无方向的 *I 行、数值 1 个或 n 个、单位刻度可为小数、
// 各段可缺省或零条记录等），语义约定见 sp_types.h 头注。
//
// 解析形态（与切块链路对齐，plan §6 文件入口任务 + 逐块解析任务）：
//   - 文件 = 文件头（版本/设计名/分隔符/四个单位行/可选名字映射表与
//     次要声明段）+ 若干网段；网段三种形态 *D_NET / *R_NET /
//     *D_PNET（物理网整段跳过 + 计数，裁定 12），每段以 *END 结束；
//   - 解析按 token 进行，不依赖换行（一行多条记录、一条记录跨行均
//     合法）；网段关键字（*D_NET / *R_NET / *D_PNET / *R_PNET）的识别
//     同样按 token、与位置无关——「行首」判定只服务切块扫描的块边界
//     （见 sp_plan_file_chunks）：行中的网段关键字不切出新块，所在行
//     留在原块，由块解析照常识别，网不丢；网与网之间的杂料 token 跳
//     过但计数（SPChunkResult::stray_content_count_——无声丢内容不可
//     接受）；
//   - 注释（// 到行尾、/* */ 可跨行）可出现在任何位置，注释里的关键
//     字不算数（词法层透明跳过）；名字映射引用（*<正整数>）当场展开
//     为原名，未登记处置见裁定 11（网名 -> 整网跳过；节点实例部分 ->
//     该条记录丢弃；引脚名部分不查表）；
//   - 取值规则（裁定 6）：多值形态 n 个冒号连接值全部读入，n == 3 取
//     下标 1、其它取下标 0；调用方显式下标优先，越界退回缺省规则并
//     计数（value_index_fallback_count），不报错；
//   - 错误边界（dev-rules §7.2 两类）：
//     文件级（抛 std::runtime_error，流程层让文件入口任务失败透出）——
//     文件不可读；gzip 魔数（流程层负责解压，直连接口不支持）；首个
//     非注释非空行不是 *SPEF；头部缺 *C_UNIT / *R_UNIT；单位词不在
//     合法集合；刻度非正数；分隔符取值非法；名字映射条目残缺；块注
//     释未闭合。
//     段级（跳过该网 + 计 broken_net_count，不影响其它网）——网段头
//     残缺；段内记录残缺或数值非法；进入合计的值为负（行首总电容、
//     *CAP 记录值）按段级破损——负值不能进入合计；段内不认识的 * 关
//     键字；*END 缺失（到文件尾或下一个网段关键字）。
//     符号校验口径（裁定）：只对进入合计的值做——不消费的数值字段
//     （电阻、电感、缩减网 π 模型与埃尔莫尔延迟、*L、*S）只要求是合
//     法数值，负值接受、不报错：不消费的值不该让已收录的合计整网丢
//     掉（上游读取器对缩减网埃尔莫尔延迟同样不查符号）。多值形态下
//     只看选中参与合计的那个值，未选中的值不触发符号校验。
//
// 接口分两层：*_file 直连接口（读文件 + gzip 魔数嗅探）；*_text 内存
// 文本接口（单测 / 文件入口任务切块后的逐块解析复用）。

#include <emir/spef/cpp/sp_types.h>

namespace fly {

// 解析头段文本（text = 头段文本；遇到首个网段关键字即视为头部结束
//——按 token 识别、与位置无关，行中关键字同样结束头部）。文件级语
// 法错误抛 std::runtime_error。source_name 仅作 source_file_ 可追溯
// 标注。
SPFileHeader sp_parse_header_text(const CMString& text,
                                  const CMString& source_name);

// 解析 SPEF 文件的头部（整读至首个网段关键字）。文件不可读 / gzip
// 魔数 / 文件级语法错误抛 std::runtime_error。
SPFileHeader sp_parse_header_file(const CMString& path);

// 切块扫描（plan §6 文件入口任务 T1 口，同 timing 的顶层构造边界切块
// 模式）：头段 = 文件开头到首个网段关键字的字节偏移；其后按网段边界
// 切块——累计字节超过 chunk_size 后在下一个网段关键字处收口；单个网
// 段超过 chunk_size 不拆分（整段进一块）；每块区间从块内首个网段起点
// 到下一块首个网段起点，最后一块到文件尾。纯头部文件 = 零块。
// 「行首（前面允许空白与注释）」判定只服务此处：仅行首网段关键字作
// 为块边界候选——行中的网段关键字不切出新块，它所在的行留在原块，由
// 块解析照常识别（网段识别按 token、与位置无关，该网不丢；块文本即
// 使以 *END 或杂料开头也有兜底——杂料跳过 + stray_content_count_ 计
// 数）。一次扫描同时产出切块计划与文件头（header_out——文件入口任务
// 与逐块解析任务共享，流程层不必为拿文件头再整读一遍大文件；同波形
// wf_plan_file_chunks(path, chunk_size, WFVarTable& vars_out) 先例形状），
// 扫描中已做的全部文件级校验（缺单位/非法单位词/分隔符/名字映射等）
// 照旧生效。头部解析与网段关键字识别共用同一套词法，文件不可读 /
// gzip 魔数 / 文件级错误照文件级口径抛 std::runtime_error。
//
// 取舍说明：文件入口整读而非流式——跟随波形模块先例，优先保证切块
// 扫描 / 头部解析 / 块解析共用同一套词法（禁止双份关键字识别）；若
// GB 级文件成为常态，演进方向是让词法器接受分块供数，而不是另写一
// 份关键字识别。
SPFileChunkPlan sp_plan_file_chunks(const CMString& path, uint64_t chunk_size,
                                    SPFileHeader& header_out);

// 解析若干完整网段文本（text = 逐块解析任务的块文本，不含文件头；
// 块区间以网段关键字行首为界——整网自包含）。单位换算/名字映射/分隔
// 符取自 header（文件入口任务解析产物，块任务共享不重解析）。取值下
// 标用「值 + 是否显式给定」两参数表达（不给定时走缺省规则——裁定 6）。
// 段级破损跳过 + 计数，不抛出。source_name 仅作统计标注。
SPChunkResult sp_parse_chunk_text(const CMString& text,
                                  const SPFileHeader& header,
                                  uint32_t value_index, bool value_index_given,
                                  const CMString& source_name);

// 解析整份 SPEF 内存文本（头部 + 全部网段）：header_out 收头部解析产
// 物（含全部文件级校验）。文件级语法错误抛 std::runtime_error。
SPChunkResult sp_parse_spef_text(const CMString& text,
                                 SPFileHeader& header_out, uint32_t value_index,
                                 bool value_index_given,
                                 const CMString& source_name);

// 解析 SPEF 文件（整读）。文件不可读 / gzip 魔数 / 文件级语法错误抛
// std::runtime_error。
SPChunkResult sp_parse_spef_file(const CMString& path,
                                 SPFileHeader& header_out, uint32_t value_index,
                                 bool value_index_given);

// 解析 SPEF 文件（整读，头部产物不外送——单测与整文件回归用）。
SPChunkResult sp_parse_spef_file(const CMString& path);

// 读文件的字节区间 [start, end)（切块链路的块任务口：把切块计划的字
// 节区间读成文本再交给 sp_parse_chunk_text）。文件不可读、或区间越出
// 文件大小 → 抛 std::runtime_error；start == end 合法（空文本）。
CMString sp_read_byte_range(const CMString& path, uint64_t start,
                            uint64_t end);

}  // namespace fly
