#pragma once

// VCD（Value Change Dump，值变化转储文件）解析器：IEEE 1364 标准四态
// ASCII 格式（waveform db 首版输入锁定口径，plan §2）。按 dev-rules §4
// 自研 C++ 解析器；真实产物特征权威参照 = qa/emir/data/waveform/
// README.md（Icarus Verilog 10.3 实测五特征：根 scope = 设计名、分段转
// 储时间戳从 0 重开、id 码复用、重复值事件行、force 语义）。
//
// 语义约定（plan §2/§5.1/§9/§13 已裁定）：
//   - 单位：$timescale 档位（1|10|100 × s|ms|us|ns|ps|fs）统一换算飞秒
//     int64（裁定 9）；事件时刻 = 文件时间戳 × 档位 fs 因子；
//   - 名字：var 名 = scope 全路径 + 名（层级分隔符归一 '/'，根 scope
//     保留——剥除属换算层职责，plan §7）；
//   - id 码复用 = 别名信号共享值轨道（README 特征 3：同 id 多名同值序
//     列）；事件按值轨道存储，var 表提供名字查找口；
//   - 向量 b 记录最高位在前、LSB 右对齐（值串最右字符 = 位权 2^0 位，
//     短值左侧补 0），按位拆解、仅变化位产生事件条目（plan §2 无存储
//     放大口径）；标量与向量统一做值等价过滤——与当前值相同的重复记
//     录行不产事件（README 特征 4/5 幂等口径），观测计数；向量轨道的
//     标量值行 = 全位同值速记（真实工具 $dumpoff 段 x 标量行形态），
//     等价判定覆盖全位（裁定 IV-7——仅查位 0 会漏置高位）；整读解析
//     的值等价过滤属解析语义，切块链路的过滤在 T3 合并端（裁定 V-2）；
//   - $dumpoff = 全值轨道置 x（段内显式行照常解析、段内未现轨道合成 x
//     事件）；$dumpvars/$dumpall/$dumpon = 段内值行照常解析（全量重
//     dump 语义）；$dumpvars 可带 (level, hierarchy) 参数形态；
//   - 词法单点（裁定 IV-5）：标量取值/时间档位/转储参数用 wf_types 的
//     wf_scalar_value/wf_parse_timescale/wf_dump_keyword；
//   - 条目级破损跳过 + 计数：坏 $var 声明、未知 id 码值行、非法位串、
//     同 id 宽度冲突；real/event 类型 var 声明与 real 值行、EVCD 强度
//     （S 前缀）/port（p 前缀）记录 = 不支持构造跳过 + 计数（按「记录
//     token + 邻随 id token」消费，WFMG::0012 家族）；
//   - 流级破损抛 std::runtime_error（对齐 tm_parser 语义）：$date/
//     $version/$timescale 段未闭合、$timescale 缺失或档位非法、
//     $enddefinitions 缺失、$scope 未闭合/$upscope 下溢、$dump 段未闭
//     合、时间戳回退、时间戳 token 非法、时间戳 × 档位 uint64 溢出、
//     无 $var 声明；文件不可读同抛；gzip 魔数（0x1f 0x8b）嗅探命中 →
//     明确报错指引不支持压缩 VCD（plan §2/§12 演进项）。

#include <emir/waveform/cpp/wf_types.h>

namespace fly {

// 解析 VCD 文件（整读）。文件不可读或流级语法错误抛 std::runtime_error。
WFVcdFile wf_parse_vcd_file(const CMString& path);

// 解析内存文本（单测 / 小文件）。source_name 仅作 source_file_ 可追溯
// 标注。
WFVcdFile wf_parse_vcd_text(const CMString& text,
                            const CMString& source_name);

// 切块扫描（plan §6 文件入口任务 T1 口，裁定 V-3 纯字节切块）：顺序扫
// 描事件段，#时间戳行边界收口切块（chunk_size 字节界后的首个 #时间戳
// 行）；首块起点 = min($dumpvars 段起点, 首个 # 行起点)——初值事件段
// 不可丢失（裁定 IV-6）。不维护全 var 当前值表、不产出任何快照（快照
// 已删）。产出切块计划（含头部 $timescale 档位——timescale_fs_/
// timescale_sec_，块内 #时间戳换算与逐文件统计的档位输入）+ 头部 var
// 表（出参——文件入口任务与逐块任务共享，块任务不重解析头部文本）。
// 文件不可读/gzip 魔数/流级语法错误抛 std::runtime_error。
WFFileChunkPlan wf_plan_file_chunks(const CMString& path, uint64_t chunk_size,
                                    WFVarTable& vars_out);

}  // namespace fly

