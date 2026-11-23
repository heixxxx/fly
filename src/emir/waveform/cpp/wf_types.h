#pragma once

// =============================================================================
// waveform db 解析边界领域结构（VCD = Value Change Dump，值变化转储文件，
// IEEE 1364 标准 ASCII 四态格式；语法权威口径 = waveform db 立项方案
// docs/emir/waveform-db-plan.md §2 + 真实产物五特征
// qa/emir/data/waveform/README.md「VCD 名字空间结构」节）。
//
// 职责边界：本头只定义「文件 → 结构」的解析产物——名字保持解析边界
// 原文形态（scope 全路径 + 名，层级分隔符归一 '/'；根 scope 剥除与
// (instance, pin) 名字换算属 db flow 换算层职责，plan §7）。分区正式
// 对象（落库形态 WFPartitionWaveform 等）随建库 flow 任务另落
// wf_partition.h（后续任务）。
//
// 结构：
//   WFValue         四态值枚举（2 bit 值域：ZERO/ONE/X/Z）
//   WFVarKind       $var 类型枚举（wire/reg/三态族；real/event 不支持
//                   功耗语义——解析跳过 + 计数，plan §5.1/WFMG::0012）
//   WFVar           单条 $var 声明：id 码 + scope 全路径名 + 宽度 + 位区间
//   WFEvent         逐位事件（时刻 fs + 四态值）
//   WFTrackEvents   单值轨道（id 码）逐位事件表——id 码复用（别名信号）
//                   共享一条轨道（值载体语义，README 特征 3：同 id 多名
//                   同值序列，别名一致由共享结构恒真保证）
//   WFVarTable      单文件 var 表：声明表 + 名字查找口 + 值轨道序（temp）
//   WFVcdFile       单文件整读解析产物容器：头部元信息 + var 表 + 轨道
//                   事件表 + 解析级计数（对齐 TMTimingFile 先例形态）
//   WFFileChunk     单块切片：字节区间 + 时间范围（temp；裁定 V-3 块首
//                   快照已删——统计与等值过滤挪 T3 合并，快照两用途消失）
//   WFFileChunkPlan 单文件切块计划：头段公共前缀区间 + 块表（temp）
//
// 词法公共单点（裁定 IV-5）：标量取值 / 时间档位换算 / 转储参数三组词
// 法提取为本头函数（wf_scalar_value / wf_timescale_unit_fs /
// wf_parse_timescale / wf_dump_keyword），解析器（wf_parser）与块扫描
// 器（wf_partition ChunkScanner）共用，禁止双份维护。
//
// 单位约定：全部时间值已换算飞秒（fs，uint64；$timescale 全部档位
// 1|10|100 × s|ms|us|ns|ps|fs 无损承载——第二轮裁定 9）；事件时刻 =
// 文件时间戳 × 档位 fs 因子。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>

#include <cstdint>

namespace fly {

// 四态值（2 bit 值域；x = 未知态，z = 高阻态）
enum class WFValue : uint8_t { ZERO = 0, ONE = 1, X = 2, Z = 3 };

// $var 类型（IEEE 1364 $var 声明 type 字段；三态族别名归并观测——首版
// 无强度语义）。real/event 不支持功耗语义：解析跳过 + 计数
//（unsupported_var_count_，WFMG::0012 家族——plan §5.1）
enum class WFVarKind : uint8_t {
    WIRE = 0,    // wire（VCD 最常见）
    REG = 1,     // reg
    TRI = 2,     // tri / tri0 / tri1
    WAND = 3,    // wand / triand
    WOR = 4,     // wor / trior
    TRIREG = 5,  // trireg
    PORT = 6,    // port
    OTHER = 7,   // 其余可转储类型兜底（integer/time/parameter 等）
    REAL = 8,    // real / realtime——不支持（跳过 + 计数）
    EVENT = 9,   // event——不支持（跳过 + 计数）
};

// real/event = 不支持构造（WFMG::0012）
bool wf_var_kind_supported(WFVarKind kind);

// 类型文本 → WFVarKind（大小写不敏感；未知类型 → OTHER 可转储兜底）
WFVarKind wf_parse_var_kind(const CMString& text);

// —— 词法公共单点（裁定 IV-5：解析器与块扫描器共用，单点维护）——

// 标量字符 → 四态值（'0'/'1'/'x'/'z'，大小写不敏感；其余字符 = false）
bool wf_scalar_value(char c, WFValue& out);

// 档位单位 → fs 因子（s/ms/us/ns/ps/fs；非法单位 = 0。×1|10|100 全档位
// ≤ 1e17，int64 无损承载——裁定 9）
uint64_t wf_timescale_unit_fs(const CMString& unit);

// $timescale 值解析（"<数字><单位>" 拼接形态，数字与单位间空格容差，
// Icarus 三行分 token 形态由调用方拼接后传入）→ fs 因子 + 原值换算秒；
// 非法档位 = false（流级 raise 与否由调用方裁定——解析器/嗅探同口径）
bool wf_parse_timescale(const CMString& joined, uint64_t& fs_out,
                        double& sec_out);

// 转储系关键字词法：剥 '(' 参数后缀得关键字本体（$dumpvars(1, top) 单
// token 形态）。返回 = 是否转储系关键字（$dumpvars/$dumpall/$dumpon/
// $dumpoff）；args_follow = 括号参数未在本 token 内闭合（调用方按自有
// token 流续扫至含 ')' 的 token）
bool wf_dump_keyword(const CMString& tok, CMString& kw, bool& args_follow);

// 逐位事件（向量拆位产物；time = 文件时间戳 × 档位 fs 因子）
struct WFEvent {
    uint64_t time_fs_ = 0;
    WFValue value_ = WFValue::X;

    FLY_SERIALIZE(time_fs_, value_)
};

// 单条 $var 声明（plan §5.1）。名字 = scope 全路径 + 名（层级分隔符归一
// '/'，根 scope 保留——剥除属换算层职责）；位区间存声明原值
//（[left:right]；has_range_ = false = 无区间声明的标量形态）。
class WFVar {
public:
    CMString id_code_;          // 值轨道 id 码（可复用 = 别名信号）
    CMString name_;             // scope 全路径 + 名（分隔符 '/'）
    uint32_t width_ = 1;        // 位宽（标量 = 1）
    bool has_range_ = false;    // 位区间声明存在
    int64_t range_left_ = 0;    // 位区间左值（声明序首值，如 [3:0] 的 3）
    int64_t range_right_ = 0;   // 位区间右值
    WFVarKind kind_ = WFVarKind::WIRE;

    bool supported() const { return wf_var_kind_supported(kind_); }

    FLY_SERIALIZE(id_code_, name_, width_, has_range_, range_left_,
                  range_right_, kind_)
};

// 单值轨道逐位事件（向量拆位产物；仅变化位有条目——plan §2 无存储放大
// 口径）。bits_[i] 时间升序，i = 0 对应二进制值串最右字符（位权 2^0，
// 短值左侧补 0）；位名下标与声明方向的换算属换算层职责。
class WFTrackEvents {
public:
    CMString id_code_;
    uint32_t width_ = 0;                // 轨道宽度（首见声明宽度）
    CMVector<CMVector<WFEvent>> bits_;  // bits_.size() = width_（各升序）

    FLY_SERIALIZE(id_code_, width_, bits_)
};

// 单文件 var 表（plan §5.1：文件入口任务解析头部产出，temp 对象逐块任
// 务共享，块任务不重解析头部文本）。real/event 声明不入表（跳过 + 计
// 数）；值轨道 = id 码去重（声明首现序）——快照值表与事件轨道表共用此
// 序约定。
class WFVarTable {
public:
    CMVector<WFVar> vars_;              // $var 声明序（全部支持类型声明）
    CMVector<CMString> track_ids_;      // 值轨道 id 码（首现序去重）
    CMVector<uint32_t> track_widths_;   // 各轨道位宽（与 track_ids_ 平行）

    // —— 运行时索引（重建件不序列化；反序列化消费前调 rebuild_indexes）
    // id 码 → 轨道下标；名字（scope 全路径 + 名）→ vars_ 下标（同名首现
    // 优先）——
    CMUnorderedMap<CMString, size_t> track_index_;
    CMUnorderedMap<CMString, size_t> name_index_;

    static constexpr size_t kNoTrack = static_cast<size_t>(-1);

    void rebuild_indexes();
    size_t track_count() const { return track_ids_.size(); }
    const WFVar* find_by_name(const CMString& name) const;
    // id 码 → 轨道下标（未登记返回 kNoTrack）
    size_t find_track(const CMString& id_code) const;

    FLY_SERIALIZE(vars_, track_ids_, track_widths_)
};

// 单文件整读解析产物容器（对齐 TMTimingFile 先例形态：头部元信息 + 表
// 结构 + 弃收/兜底计数；计数器逐字段累加，flow 侧消费时经 message 一次
// 汇总——plan §9 消息族）。
class WFVcdFile {
public:
    // —— 头部元信息 ——
    CMString date_;                      // $date 原文（token 空格拼接；
                                         // 解析器不消费）
    CMString version_;                   // $version 原文（同上）
    CMString source_file_;               // 来源文件完整路径（可追溯）
    double timescale_sec_ = 1e-9;        // $timescale 原值换算秒（1ns=1e-9）
    uint64_t timescale_fs_ = 1000000;    // 档位 fs 因子（1 时间戳单位 =
                                         // 多少 fs；缺省 1ns 档）
    uint64_t t_start_fs_ = 0;            // 首时间戳（fs）
    uint64_t t_end_fs_ = 0;              // 末时间戳（fs）
    uint64_t duration_fs_ = 0;           // 总时长 = t_end − t_start
                                         //（无时间戳 = 0；plan §5.2
                                         // WFFileStats 同口径）

    WFVarTable vars_;
    CMVector<WFTrackEvents> tracks_;     // 值轨道事件表（序 = track_ids_）

    // —— 解析级计数（WFMG 家族见 plan §9）——
    // real/event var 声明跳过数（0012）
    uint64_t unsupported_var_count_ = 0;
    // real 值行 / EVCD 强度·port 记录行跳过数（0012）
    uint64_t unsupported_record_count_ = 0;
    // 值等价重复记录行数（README 特征 4/5：真实 VCD 含与当前值相同的
    // 记录行——值等价过滤后不产事件，观测计数）
    uint64_t duplicate_value_count_ = 0;
    // 条目级语法破损跳过数（坏 $var 声明/未知 id 码值行/非法位串/同 id
    // 宽度冲突）
    uint64_t bad_record_count_ = 0;
    // 未知 $关键字段跳过数
    uint64_t unknown_construct_count_ = 0;

    void rebuild_indexes() { vars_.rebuild_indexes(); }

    FLY_SERIALIZE(date_, version_, source_file_, timescale_sec_,
                  timescale_fs_, t_start_fs_, t_end_fs_, duration_fs_,
                  vars_, tracks_, unsupported_var_count_,
                  unsupported_record_count_, duplicate_value_count_,
                  bad_record_count_, unknown_construct_count_)
};

// 单块切片（temp）：字节区间 + 时间范围。块 i = [start_, end_)，块界 =
// #时间戳行边界（plan §5.1）；相邻块首尾衔接（chunk[i].end_ ==
// chunk[i+1].start_），末块 end_ = 文件长度。首块起点 = min($dumpvars
// 段起点, 首个 # 行起点)——初值事件段不可丢失（裁定 IV-6）。无 # 时间
// 戳行的纯 $dumpvars 形态 = 单块兜底（$dumpvars 段起至文件尾，时间范
// 围 [0,0]）。
struct WFFileChunk {
    uint64_t start_ = 0;
    uint64_t end_ = 0;
    uint64_t t_start_fs_ = 0;   // 块内首时间戳（fs）
    uint64_t t_end_fs_ = 0;     // 块内末时间戳（fs）

    FLY_SERIALIZE(start_, end_, t_start_fs_, t_end_fs_)
};

// 单文件切块计划（temp；plan §5.1）：档位（头部 $timescale 解析产物
//——块内 #时间戳换算与逐文件统计的档位输入，随计划一次产出，文件入
// 口任务免二次整读嗅探）+ 头段公共前缀区间（$enddefinitions 前——var
// 声明段；块任务经 WFVarTable 复用不重解析头部文本）+ 块表。纯字节切
// 块（裁定 V-3）：不维护全 var 当前值表、不产出任何快照；首块起点 =
// min($dumpvars 段起点, 首个 # 行起点)——$dumpvars 初值事件段落首块
//（裁定 IV-6 修复）。
class WFFileChunkPlan {
public:
    CMString file_name_;
    uint64_t file_size_ = 0;
    uint64_t timescale_fs_ = 1000000;    // 档位 fs 因子（1 时间戳单位 =
                                         // 多少 fs；缺省 1ns 档——与
                                         // 解析器缺省一致）
    double timescale_sec_ = 1e-9;        // $timescale 原值换算秒
    uint64_t header_start_ = 0;  // 头段起点（= 0，$date 起）
    uint64_t header_end_ = 0;    // 头段终点（$enddefinitions 关键字起点）
    CMVector<WFFileChunk> chunks_;

    FLY_SERIALIZE(file_name_, file_size_, timescale_fs_, timescale_sec_,
                  header_start_, header_end_, chunks_)
};

}  // namespace fly
