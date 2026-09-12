#pragma once

// =============================================================================
// S8 全局密度图合并 + 分区决策（方案 design-db-plan.md §4 + 2026-09-12/13
// 用户裁定）。
//
// 数据结构：
//   DSSubPartition    分区描述——core_rect（密度网格切分直接产出）+
//                     extend_rect（电阻提取完整图形扩展域：非边缘方向
//                     core 边界向外扩 2×w_eff，最外围方向不截断、直接开
//                     到 int32 极值；相邻分区 extend 允许重叠）。
//   DSDensityWeights  通道比重（合成负载 = w_inst×inst + w_metal×Σ层
//                     metal_l + w_via×Σ层 via_l）；逐层系数接口保留
//                     （layer_factors_，未命中 = 1，本期不暴露 alpha 键，
//                     裁定 ⑥）。默认 instance=6 / metal=2 / via=2。
//
// 算法（声明处参数按引用借用，不拷贝大体量产物）：
//   ds_merge_global_density  层级树后序自底向上合并（实现取线性等价形
//                     式：每个树节点的 def 局部密度图按其到根的复合变换
//                     撒入全局格网——与逐级平移叠加在变换复合结合律下数
//                     学等价，见 .cpp 头注释）；格值分摊按裁定 D10 A：
//                     局部格计数与全局格交叠面积成比例撒入（交叠面积中
//                     间量 int64 + 最大余数法保证总量守恒）。
//   ds_decide_partitions  行列矩形网格切分：切线按行/列合成负载前缀和
//                     等分、吸附格边界；分区数来源优先级 target_partitions
//                     （'{x}x{y}' 直切）> partition_count >
//                     partition_target_density（默认 150000）；alpha 非
//                     法值一律 DSGN::0013 提醒后回退，不 raise（dev-rules
//                     §7）。
//
// 本头文件自包含（不依赖 ds_types.h——DSDesign.partitions_ 字段引用
// DSSubPartition 完整类型；两个算法函数以前置声明引用其余类型）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>
#include <geometry/cpp/geometry_types.h>

#include <cstdint>
#include <unordered_map>

namespace fly {

class DSDensityGrid;
class DSStack;
class DSHierTree;
class DSBlockBuildData;
class DSNetBuildData;

// 分区描述：core = 密度切分直接产出（格边界对齐）；extend = 电阻提取完
// 整图形扩展域（非边缘方向 +2×w_eff，最外围方向 int32 极值，允许相邻
// 重叠——2026-09-12 裁定 2）
struct DSSubPartition {
    // 行主序分区号（(xp, yp) → yp*nx + xp，产出序）
    uint32_t partition_id_ = 0;
    // 密度网格切分直接产出（全局 DBU，格边界吸附）
    GEORect core_rect_;
    // 电阻提取完整图形扩展域（全局 DBU；最外围方向 = int32 极值，单分区
    // 常态即 (INT_MIN, INT_MIN, INT_MAX, INT_MAX)）。**消费禁令**：极值域
    // 矩形禁止调用 width()/height()——GEORect 的 width() 断言能通过
    //（x_high >= x_low 成立），但 INT_MAX − INT_MIN 的 int32 减法是溢出
    // UB（实测静默 wrap 得负值），比断言报错更危险；消费方（S9 flatten /
    // 电阻提取）只允许使用四个坐标分量做包含/交叠判定，面积与宽度量
    // 一律在需要时经 int64 域自算。
    GEORect extend_rect_;

    FLY_SERIALIZE(partition_id_, core_rect_, extend_rect_)
};

// 通道比重（合成负载 = w_inst×inst + w_metal×Σ层 metal_l + w_via×Σ层
// via_l，§4.1 加权折叠只在 S8 分区决策时做一次）
struct DSDensityWeights {
    double instance_ = 6.0;
    double metal_ = 2.0;
    double via_ = 2.0;
    // 逐层密度系数（键 = layer id；裁定 ⑥：低层电阻高/图形多、系数自高
    // 层向低层递增；首版全 1、接口保留输入，本期不暴露 alpha 键——未登
    // 记层按 1.0 计）
    std::unordered_map<uint32_t, double> layer_factors_;

    // 层系数读取（未登记 = 1.0）
    double layer_factor(uint32_t layer_id) const {
        const auto it = layer_factors_.find(layer_id);
        return it == layer_factors_.end() ? 1.0 : it->second;
    }
};

// S8 密度合并：层级树自底向上 + 格值分摊（裁定 D10 A）→ 全局密度图。
// 格网参数 = 根 block 定义局部格网（原点 = 根 DIEAREA 左下角、bin 同
// local、ceil 覆盖根 DIEAREA bbox——2026-09-12 裁定 7）。blocks/nets 按
// def_paths 序一一对齐（同 ds_build_hier_tree 入参约定；节点 block 名
// → def 序重名保留首份）。无 DEF（空树）或根 def 无 DIEAREA（格网未配
// 置）→ 返回未配置空图（空结果放行，dev-rules §7）。
DSDensityGrid ds_merge_global_density(
    const DSHierTree& tree,
    const CMVector<const DSBlockBuildData*>& blocks,
    const CMVector<const DSNetBuildData*>& nets);

// S8 分区决策 → 分区表（行主序产出；跳过空段——切线数超过格数时段长可
// 为 0，零格段不产出分区）。global 未配置 → 空表。alpha 键语义：
//   target_partitions  '{x}x{y}'（x/y ≥ 1），空串 = 未设置；解析失败
//                      DSGN::0013 提醒后视为未设置
//   partition_count    > 0 生效；<= 0 = 未设置
//   target_density     > 0 生效；<= 0 回退默认 150000
//   优先级 target_partitions > partition_count > target_density（裁定 4）
// w_eff = 最高有效层 default_width（有效层 = 全局合并后金属格值总量 > 0
// 的最高 ROUTING 层，不看层表顺序；无有效层 = 0，裁定 2）。
CMVector<DSSubPartition> ds_decide_partitions(
    const DSDensityGrid& global, const DSStack& stack,
    const DSDensityWeights& weights, const CMString& target_partitions,
    int partition_count, int64_t target_density);

// S8 分区决策 alpha 键回退默认（裁定 4：目标密度 15 万——默认下每分区
// 约 10-20 万 leaf instance）
inline constexpr int64_t kDefaultPartitionTargetDensity = 150000;

}  // namespace fly
