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

namespace fly {

// 库头属性（simple/complex 统一转文本保存，保留原始精度语义）
class LIBHeaderAttr {
public:
    CMString name_;
    CMString value_;

    FLY_SERIALIZE(name_, value_)
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
    CMVector<LIBHeaderAttr> extra_attrs_;
    CMVector<LIBInternalPower> internal_powers_;
    CMVector<LIBTimingArc> timings_;

    FLY_SERIALIZE(name_, direction_, capacitance_, rise_capacitance_,
                  fall_capacitance_, max_capacitance_, extra_attrs_,
                  internal_powers_, timings_)
};

class LIBCell {
public:
    CMString name_;
    double area_ = 0.0;
    bool is_sequence_cell_ = false;   // 含 ff/latch 组（时序单元）
    CMVector<LIBHeaderAttr> extra_attrs_;
    CMVector<LIBPin> pins_;

    FLY_SERIALIZE(name_, area_, is_sequence_cell_, extra_attrs_, pins_)
};

// lib db 的顶层整合容器：多文件分布式解析的结果汇整于单一对象
// （docs/emir-data-flow.md §4 裁定 10），下游（design db 的 cell id 分配、
// power/current 计算引擎）的统一读取入口。
class LIBLibrary {
public:
    CMString name_;   // 库名（library 组名；多文件时首个文件的库名，重复名 Warn）

    // 库头属性全量（time_unit/voltage_unit/capacitive_load_unit/slew_derate_* 等）
    CMVector<LIBHeaderAttr> header_attrs_;

    // 模板集（平行数组；同一模板被全库表共享，消除逐表重复索引）
    CMVector<CMString> template_names_;
    CMVector<CMLookupTableTemplate> templates_;

    CMVector<LIBCell> cells_;

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

    FLY_SERIALIZE(name_, header_attrs_, template_names_, templates_, cells_,
                  skipped_group_names_, skipped_group_counts_)
};

}  // namespace fly
