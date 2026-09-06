#pragma once

// =============================================================================
// CMLookupTable — fly 框架级公共查找表结构
//
// Liberty 等 EDA 格式的查找表族（时序表/功耗表/电流波形表/统计表）的统一
// 保存与查询结构。两级组织消除逐表重复索引的存储开销：
//   - CMLookupTableTemplate：轴变量名 + 各轴默认索引（库内少量、跨表共享）
//   - CMLookupTable：表数据（values 行优先展平）+ 模板引用或自足轴定义
//
// 表的两种模式：
//   - 模板引用模式：template_name_ 非空，轴定义由所属容器（如 LIBLibrary）
//     的模板集提供，经 resolve_template 展开为可插值状态；index_sets_ 可按轴
//     局部覆盖（覆盖轴非空、未覆盖轴为空）。
//   - 自足模式：template_name_ 为空，variable_names_/index_sets_ 内联完整。
//
// values 行优先（第一轴变化最慢）——与 Liberty values 语义一致（index_1 为行）。
// interpolate 为 N 维多线性插值（维度 ≤ 3，坐标超界 clamp 到边界；单点轴
// 合法，取该点常值）。
//
// 算法引擎（C++）直接调用 interpolate，语言间零开销；经 FLY_SERIALIZE_*
// 序列化（配合 FLY_EXPORT_SERIALIZE 绑定）后可作为 fly db 对象持久化。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <common/serialization/cpp/serialization_macros.h>

#include <cstddef>

namespace fly {

class CMLookupTableTemplate {
public:
    // 各轴变量名（如 input_net_transition / total_output_net_capacitance）
    CMVector<CMString> variable_names_;
    // 各轴默认索引（Liberty 语义：升序）
    CMVector<CMVector<double>> index_sets_;

    size_t dim() const { return variable_names_.size(); }

    FLY_SERIALIZE(variable_names_, index_sets_)
};

class CMLookupTable {
public:
    // 表名（如 rise_power / fall_power / cell_rise；容器内唯一性由容器保证）
    CMString name_;
    // 轴数（1..3）。模板引用模式下也显式携带，未 resolve 即可校验/分配。
    int dim_ = 0;
    // 引用的模板名；空 = 自足模式。
    CMString template_name_;
    // 自足模式：各轴变量名；模板模式：resolve 前可为空，resolve 后填充。
    CMVector<CMString> variable_names_;
    // 自足模式：各轴完整索引；模板模式：仅覆盖轴非空，resolve 后补全。
    CMVector<CMVector<double>> index_sets_;
    // 表数据，行优先展平（第一轴变化最慢），长度 = ∏各轴索引长度。
    CMVector<double> values_;

    // 用模板展开为可插值状态：补齐 variable_names_ 与未覆盖轴的 index_sets_。
    // values_.size() 与展开后轴长度乘积不符时抛 std::invalid_argument。
    void resolve_template(const CMLookupTableTemplate& tmpl);

    // 已就绪（dim 匹配、各轴索引非空升序、values 数量匹配）。
    bool is_ready() const;

    // N 维多线性插值。coords 顺序 = values_ 各轴顺序（index_1 最慢）。
    // 坐标超界 clamp 到端点；维度/数据不匹配抛 std::invalid_argument。
    double interpolate(const CMVector<double>& coords) const;

    FLY_SERIALIZE(name_, dim_, template_name_, variable_names_, index_sets_, values_)
};

}  // namespace fly
