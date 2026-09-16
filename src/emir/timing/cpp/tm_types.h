#pragma once

// =============================================================================
// timing db 解析边界领域结构（TWF = Timing Window File，时序窗口文件，
// 楷登 Innovus write_timing_windows 命令产物，含 -power_compatible 风味；
// 字节级语法权威源 = 该命令 25.10 版手册，镜像于 echo-edai/innovus-
// tcl-helper 仓库）。
//
// 职责边界：本头只定义「文件 → 结构」的解析产物——名字保持解析边界
// 原文形态（带反斜杠转义清理）；实例/引脚名 → 全局 id 换算与分区路由
// 归 db flow（消费 design db 名字体系）。分区正式对象（TMPartitionTiming
// 等）随建库 flow 立项另落文件。
//
// 结构：
//   TMRange       范围对（min:max；文件单标量形态解析为 min = max）
//   TMClock       WAVEFORM 条目：时钟名 + 周期 + 两个边沿时刻（文件值
//                 × TIME_SCALE 换算为 ns 存储）
//   TMNameTiming  逐名时序条目：缺省风味键 = 网名（NET 条目）、-pin 风
//                 味键 = 引脚名（PIN 条目）；RTW/Rt/FTW/Ft 四组值 +
//                 常量/多时钟源/字段存在标记
//   TMTimingFile  单文件解析产物容器：头部元信息 + 时钟表 + 条目表 +
//                 弃收/兜底计数（来源可追溯 source_file_）
//
// 单位约定：全部时间值已换算 ns（文件原始值 × TIME_SCALE × 1e9；
// TIME_SCALE 缺省 1e-9 = 文件值即 ns）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <common/types/cpp/flags_macro.h>
#include <container/cpp/container_aliases.h>
#include <emir/common/cpp/emir_ids.h>

#include <cstdint>

namespace fly {

// 无时钟归属哨兵（CAUSED_BY NULL / 引用了未登记 WAVEFORM 的时钟名）：
// CMClockId 默认哨兵（kInvalid = UINT32_MAX，2026-09-16 收编强类型——
// 原 kTMNoClock 裸常量删除，值语义不变）

// 范围对：到达窗口与翻转时间的统一形态
struct TMRange {
    double min_ = 0.0;
    double max_ = 0.0;

    FLY_SERIALIZE(min_, max_)
};

// WAVEFORM 条目（时钟定义；时刻已换算 ns）
class TMClock {
public:
    CMString name_;
    double period_ = 0.0;
    double posedge_ = 0.0;
    double negedge_ = 0.0;

    FLY_SERIALIZE(name_, period_, posedge_, negedge_)
};

// 逐名时序条目（NET/PIN 记录，同名跨 CAUSED_BY 分组已合并）。
// 字段存在位语义：文件中该位置为「*」= 无数据（位复位）；有效值解析
// 失败的整条记录不入库（bad record 计数，见 TMTimingFile）。
class TMNameTiming {
public:
    CMString name_;
    // 到达窗口（RTW / FTW）与翻转时间（Rt / Ft），ns
    TMRange rise_arrival_;
    TMRange fall_arrival_;
    TMRange rise_slew_;
    TMRange fall_slew_;
    // 主时钟（首现分组；NULL 源 = CMClockId 哨兵）。强类型 id（CM 族，
    // 2026-09-16 裁定——解析边界裸整型收编）
    CMClockId clock_id_;
    // 标记位：pin_kind = -pin 风味引脚条目（缺省网络条目）；
    // multi_source = 同名条目来自多于一个时钟源分组（窗口已取并集）；
    // constant = CONSTANT 常量条目（无窗口无翻转，仅名字）；
    // rise_arrival/fall_arrival/rise_slew/fall_slew = 对应值存在
    CM_FLAGS(uint8_t, pin_kind, multi_source, constant, rise_arrival,
             fall_arrival, rise_slew, fall_slew)

    FLY_SERIALIZE(name_, rise_arrival_, fall_arrival_, rise_slew_,
                  fall_slew_, clock_id_, flags_)
};

// 单文件解析产物容器。计数器逐字段累加，flow 侧消费时经 message 一次
// 汇总提醒（dev-rules §7 兜底语义）。
class TMTimingFile {
public:
    // —— 头部元信息（HEADER 构造子集，其余头部构造静默忽略）——
    CMString design_;
    CMString version_;
    // 来源文件完整路径（可追溯）
    CMString source_file_;
    // 头声明 DELIMITERS 原文（如 "/[]"——timing db strip_prefix 段级剥
    // 离与名字维度拆段消费；首字符 = 层级分隔符，未声明时空串、缺省 '/'
    // 语义见 hier_delim()。plan §7.4 实施注记：解析器收录）
    CMString delimiters_;
    // 头声明换算因子（文件值 × 因子 = 秒；缺省 1e-9）
    double time_scale_sec_ = 1e-9;
    // VOLTAGE_THRESHOLD（缺省 10/90 —— read_twf 手册缺省语义）
    double vth_low_ = 10.0;
    double vth_high_ = 90.0;
    // DEFAULT_INPUT_SLEW 原值序列（语义随工具版本，原样保存供下游参考）
    CMVector<double> default_input_slew_;

    CMVector<TMClock> clocks_;
    CMVector<TMNameTiming> entries_;

    // —— 弃收/兜底计数 ——
    // RDr/FDr（源电阻）有值但弃收次数（当前无消费者）
    uint64_t dropped_source_res_count_ = 0;
    // RSlk/FSlk（富余量）有值但弃收次数（当前无消费者）
    uint64_t dropped_slack_count_ = 0;
    // 未知构造跳过次数（含重复 WAVEFORM 时钟名）
    uint64_t unknown_construct_count_ = 0;
    // 条目级语法破损跳过次数（字段数不符/值非法/名字形态错误）
    uint64_t bad_record_count_ = 0;
    // CAUSED_BY 引用未登记 WAVEFORM 时钟名的条目分组数
    uint64_t missing_clock_count_ = 0;
    // 结尾标记计数（语义无公开说明，不入库仅观测：[0] = C、[1] = D）
    uint64_t cd_flag_c_count_ = 0;
    uint64_t cd_flag_d_count_ = 0;

    // —— 运行时索引（重建件不序列化；反序列化消费前调 rebuild_indexes）——
    CMUnorderedMap<CMString, CMClockId> clock_index_;
    CMUnorderedMap<CMString, size_t> entry_index_;

    void rebuild_indexes();
    // 层级分隔符（DELIMITERS 首字符；未声明缺省 '/'——Innovus 手册缺省
    // 语义，strip_prefix 段匹配与名字维度拆段统一走此口）
    char hier_delim() const {
        return delimiters_.empty() ? '/' : delimiters_[0];
    }
    // 条目写入 + 同名合并（窗口/翻转逐字段取并集、时钟源差异置
    // multi_source、常量位取或）；解析器逐记录调用
    void upsert_timing(TMNameTiming&& e);

    const TMNameTiming* find_entry(const CMString& name) const;
    const TMClock* find_clock(const CMString& name) const;

    FLY_SERIALIZE(design_, version_, source_file_, delimiters_,
                  time_scale_sec_, vth_low_, vth_high_, default_input_slew_,
                  clocks_, entries_, dropped_source_res_count_,
                  dropped_slack_count_, unknown_construct_count_,
                  bad_record_count_, missing_clock_count_,
                  cd_flag_c_count_, cd_flag_d_count_)
};

}  // namespace fly
