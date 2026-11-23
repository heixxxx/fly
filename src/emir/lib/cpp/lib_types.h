#pragma once

// lib 库 db 的 C++ 数据结构：LIBLibrary（整合容器）→ LIBCell → LIBPin
// → 功耗/时序弧表（框架级 CMLookupTable）。
//
// 职责边界：lib 阶段以 cell name 区分 cell（不分配 id）；cell id 由
// design db 建库时读入 LIBLibrary 后分配（docs/emir-data-flow.md §4 裁定 9）。
// 全部结构经 FLY_SERIALIZE 序列化，作为 fly db 对象持久化。

#include <container/cpp/container_aliases.h>
#include <container/cpp/lookup_table.h>
#include <common/serialization/cpp/serialization_macros.h>

#include <limits>

namespace fly {

// 库头属性（simple/complex 统一转文本保存，保留原始精度语义）
class LIBHeaderAttr {
public:
    CMString name_;
    CMString value_;

    FLY_SERIALIZE(name_, value_)
};

// 漏电功耗分项（leakage_power 组，cell 级可多组）：逐状态漏电，power 引擎
// 按 when 条件 × 输入状态概率加权求和（books §2.2 漏电逐状态计算）。
// when/related_pg_pin/power_level 原文保留；未声明属性 = 空串。
class LIBLeakagePower {
public:
    CMString when_;             // 状态条件表达式原文（空 = 无条件兜底项）
    CMString related_pg_pin_;   // 多电源分轨归属（memory VDDPE/VDDCE 场景）
    CMString power_level_;      // IO 双轨场景 CORE_VOLTAGE / IO_VOLTAGE
    double value_ = std::numeric_limits<double>::quiet_NaN();  // NaN = 组内未声明 value
    CMVector<LIBHeaderAttr> extra_attrs_;   // 其余属性全量（对齐 LIBInternalPower）

    FLY_SERIALIZE(when_, related_pg_pin_, power_level_, value_, extra_attrs_)
};

// 电源/地引脚（pg_pin 组，cell 级可多组）：PG 网络连接的库内声明，
// power/current 引擎按 related_power_pin → pg_pin → supply net 聚合。
class LIBPgPin {
public:
    CMString pg_name_;          // 组名（pg_pin(VDD) 的 VDD）
    CMString pg_type_;          // primary_power / primary_ground / internal_power / ...
    CMString voltage_name_;     // 指向库头 voltage_map 名字
    CMVector<LIBHeaderAttr> extra_attrs_;

    FLY_SERIALIZE(pg_name_, pg_type_, voltage_name_, extra_attrs_)
};

// 库级 supply 电压映射（voltage_map）：supply 名字 → 标称电压。
// 简写属性 voltage_map (VDD, 1.10); 与展开组 voltage_map (VDD) { voltage : ... }
// 两种源形态统一收集（解析器文法把简写归为 complex 属性而非组）。
class LIBVoltageMap {
public:
    CMString name_;
    double voltage_ = std::numeric_limits<double>::quiet_NaN();  // NaN = 源形态未带电压

    FLY_SERIALIZE(name_, voltage_)
};

// internal_power 组：related_pin + 功耗表集合（表名 = rise_power/fall_power 等）
class LIBInternalPower {
public:
    CMString related_pin_;
    // 非 related_pin 依赖的其余 simple 属性（如 power_down_function），全量保存
    CMVector<LIBHeaderAttr> extra_attrs_;
    CMVector<CMLookupTable> tables_;

    FLY_SERIALIZE(related_pin_, extra_attrs_, tables_)
};

// timing 组：时序弧（related_pin + unate/type + 时序表集合）
class LIBTimingArc {
public:
    CMString related_pin_;
    CMString timing_sense_;   // positive_unate / negative_unate / non_unate
    CMString timing_type_;    // combinational / rising_edge / ...
    CMVector<LIBHeaderAttr> extra_attrs_;
    CMVector<CMLookupTable> tables_;   // cell_rise/cell_fall/rise_transition/...

    FLY_SERIALIZE(related_pin_, timing_sense_, timing_type_, extra_attrs_, tables_)
};

class LIBPin {
public:
    CMString name_;
    CMString direction_;          // input / output / internal / inout
    double capacitance_ = 0.0;    // 引脚电容（receiver pin load）
    double rise_capacitance_ = 0.0;
    double fall_capacitance_ = 0.0;
    double max_capacitance_ = 0.0;
    // 供电归属（未声明 = 空串）：把 pin 功耗拆分到 PG 网/电源域的映射依据
    CMString related_power_pin_;
    CMString related_ground_pin_;
    CMString power_down_function_;   // 输出断电条件（电源关断域场景）
    CMVector<LIBHeaderAttr> extra_attrs_;
    CMVector<LIBInternalPower> internal_powers_;
    CMVector<LIBTimingArc> timings_;

    FLY_SERIALIZE(name_, direction_, capacitance_, rise_capacitance_,
                  fall_capacitance_, max_capacitance_, related_power_pin_,
                  related_ground_pin_, power_down_function_, extra_attrs_,
                  internal_powers_, timings_)
};

class LIBCell {
public:
    CMString name_;
    CMString library_name_;   // 所属 library 组名（来源可追溯）
    CMString source_file_;    // 解析来源 .lib 文件完整路径（来源可追溯）
    double area_ = 0.0;
    bool is_sequence_cell_ = false;   // 含 ff/latch 组（时序单元）
    // 默认漏电功率（cell_leakage_power，单位见库头 leakage_power_unit）：
    // NaN = 未声明（0.0 是合法声明值，不可与缺失混淆）；有逐状态分项时
    // 由 leakages_ 按 when 加权覆盖
    double cell_leakage_power_ = std::numeric_limits<double>::quiet_NaN();
    CMVector<LIBHeaderAttr> extra_attrs_;
    CMVector<LIBLeakagePower> leakages_;
    CMVector<LIBPgPin> pg_pins_;
    CMVector<LIBPin> pins_;

    FLY_SERIALIZE(name_, library_name_, source_file_, area_, is_sequence_cell_,
                  cell_leakage_power_, extra_attrs_, leakages_, pg_pins_, pins_)
};

// lib db 的顶层整合容器：多文件分布式解析的结果汇整于单一对象
// （docs/emir-data-flow.md §4 裁定 10），下游（design db 的 cell id 分配、
// power/current 计算引擎）的统一读取入口。
class LIBLibrary {
public:
    // 库头属性全量（time_unit/voltage_unit/capacitive_load_unit/slew_derate_* 等）
    CMVector<LIBHeaderAttr> header_attrs_;

    // 模板集（平行数组；同一模板被全库表共享，消除逐表重复索引）
    CMVector<CMString> template_names_;
    CMVector<CMLookupTableTemplate> templates_;

    CMVector<LIBCell> cells_;

    // 库级 supply 电压映射（voltage_map）：功耗的 I=P/V 换算与多电压域
    // 识别依据。merge 语义同 header_attrs（保留首个文件，后续文件不并入）。
    CMVector<LIBVoltageMap> voltage_maps_;

    // 未知/未收集组统计（组类型名 → 出现次数）：全量保存原则的兜底观测，
    // 发现遗漏可据此扩展解析器
    CMVector<CMString> skipped_group_names_;
    CMVector<int> skipped_group_counts_;

    // cell 名 → cells_ 下标（运行时索引，非序列化成员；
    // build_cell_index 后 find_cell 为 O(1)，索引线程安全只读）
    CMUnorderedMap<CMString, size_t> cell_index_;

    void build_cell_index();
    const LIBCell* find_cell(const CMString& name) const;   // 需先 build_cell_index

    const CMLookupTableTemplate* find_template(const CMString& name) const;
    void record_skipped_group(const CMString& type_name);

    // 把 src 并入本容器（多文件分布式解析的 merger 语义）：
    // cell 冲突 = 库版本混用——保留当前（首次出现）、抛弃后续重复，不抛
    // 异常，每次抛弃经 LIBR::0001 WARN 提醒（cell 名 + 两处来源文件路径）；
    // 模板集并入（重名先入优先）；skipped_group 统计累加；voltage_maps_
    // 与 header_attrs 同款（保留首个文件）；完成后重建 cell 索引。
    // 返回抛弃的重复 cell 数。
    size_t merge_from(const LIBLibrary& src);

    FLY_SERIALIZE(header_attrs_, template_names_, templates_, voltage_maps_,
                  cells_, skipped_group_names_, skipped_group_counts_)
};

}  // namespace fly
