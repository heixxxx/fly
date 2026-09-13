#pragma once

// =============================================================================
// S7 跨块连接归并（并查集；方案 design-db-plan.md §2.6 + 2026-09-13 裁定
// 补记①-⑤）：id 域 = global net id（local + S6 起始编号换算参与 union，
// 同一 block 定义多次实例化的网天然是不同 global id）；仅 port 相连网参
// 与（internal net 绝不入表——规模与语义红线，规模 port 级数千，单对象
// 存储不分块）；union 树全路径压缩后恒两层（find 恒一步），root 规范 =
// 等价类中层级最高（最接近树根）的网、同级取最小 global id——物理网身
// 份锚在 top 层；悬空 port（未连接任何父网的 port 网）照常入表
//（root = 自身）+ 计数提醒；S9 分区产物不换算 root（无耦合）。
//
// 两级任务形态（同 S9 两级先例）：
//   ds_collect_net_union_slice   per-DEF 局部收集（每父块 DEF 一并行任
//                                务）：该 block 定义全部实例化位置的
//                                (父网, 子网) union 边 + 本 def 的 port
//                                网 local id 集（悬空判定素材）；
//   ds_build_net_union           全局汇总（单任务）：合并全部局部边集 →
//                                两层化 + root 规范化 + 悬空计数。
//
// 对接形态（S5b 连接表为名字形态，DSNetConnectionParseNode 实现：连接
// 项按 defi 回调原样保留，port 引用 instance_name = "PIN"；S5b 连接表
// 无 local 0 条目——block 自身占位仅在 instance 表）：父网连接条目
// (子实例名, port 名) × 子网连接条目 ("PIN", port 名)——对接键 = 同一块
// 实例 + 同名 port，两侧都是字符串（无需 pin id）；同一子网连接多个
// port 且这些 port 在父层连到不同父网 → 两父网 union（电气等价，合法
// 形态）。顶层引脚连接（root 块的 ("PIN", port)）不产生跨层 union
//（root 候选，root 块 port 网不入悬空口径）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>

#include <cstdint>

namespace fly {

class DSHierTree;
class DSNetBuildData;

// union 边（电气等价对：两 global net id；规范化 a < b 存放）
class DSNetUnionEdge {
public:
    uint64_t net_a_ = 0;
    uint64_t net_b_ = 0;

    FLY_SERIALIZE(net_a_, net_b_)
};

// per-DEF 局部收集产物（临时对象，汇总合并后即弃）：
//   edges_         该 def 全部实例化位置的 (父网, 子网) 边（规范化
//                  (min, max) + 升序去重）
//   port_net_ids_  本 def 的 port 网 local net id 集（连接表含
//                  ("PIN", port) 引用的网；悬空判定由汇总任务结合树与
//                  全量边集完成——per-DEF 局部视野不知父侧连接）
//   block_name_    本 def 的 block 名（DESIGN 名 = block cell 名；汇总
//                  任务按名反查树上的实例化位置）
class DSNetUnionSlice {
public:
    CMVector<DSNetUnionEdge> edges_;
    CMVector<uint64_t> port_net_ids_;
    CMString block_name_;

    FLY_SERIALIZE(edges_, port_net_ids_, block_name_)
};

// 跨块连接归并结果（S7 正式产物，独立对象不进 DSDesign 容器——⑬ 大体
// 量独立数据独立对象同口径；本表仅 port 相连网）
class DSNetUnion {
public:
    // 成员网 global id → root（两层树：root 自映射；不在表 = 未参与
    // union 的网，find 返回自身）
    CMUnorderedMap<uint64_t, uint64_t> root_of_;
    // root → 成员 global id 列表（升序，含 root 自身；§2.6 反向索引——
    // 物理网枚举）
    CMUnorderedMap<uint64_t, CMVector<uint64_t>> members_of_;
    // 悬空计数：单成员等价类数（悬空 port 网 root = 自身，裁定 ④）
    uint64_t dangling_count_ = 0;

    // find 恒一步（两层不变式）；不在表 = 自身
    uint64_t find(uint64_t net_global_id) const;
    // root 的成员枚举（未命中 nullptr——非 root 或不在表）
    const CMVector<uint64_t>* members(uint64_t root) const;
    // 等价类总数（含单成员悬空类）
    uint64_t class_count() const;

    FLY_SERIALIZE(root_of_, members_of_, dangling_count_)
};

// per-DEF 局部收集（每父块 DEF 一调用）：parent_nets = 该 def 的 S5b 网
// 产物；实例化位置由树按 block 名反查（该 def 全部位置一次收集，树扫描
// 一遍——元数据级开销）；child_defs = 该 def 引用的各子定义网产物（编排
// 侧经 ds_net_union_child_indexes 定位后传入）。port 名在子定义无网连接
// 时无对接（父网不经此 port 下探）；子定义网产物未提供时相应边缺失
//（层级不完整设计的兜底）。
DSNetUnionSlice ds_collect_net_union_slice(
    const DSHierTree& tree,
    const DSNetBuildData& parent_nets,
    const CMVector<const DSNetBuildData*>& child_defs);

// 全局汇总（单任务）：合并全部局部边集 → 小规模并查集（路径压缩）→
// 逐类 root 规范化（树深度最小者优先、同级最小 global id——树深度经
// block_of_net 反查所属 block instance 后 parent 链上溯，port 级量小）
// → 全类重挂两层 + root → 成员反向索引 + 悬空计数。悬空 port 网 = 不在
// 任何边上的非 root 块 port 网（按位全局 id 逐实例化位置判定）；root 块
// 的 port 网（顶层引脚连接）不入表不入悬空口径。无树 → 空结果放行
//（dev-rules §7）；**无边仍有悬空**（全 port 未连父网的输入形态——
// 悬空判定不依赖边存在，review 2026-09-13 修正）。
DSNetUnion ds_build_net_union(const DSHierTree& tree,
                              const CMVector<const DSNetUnionSlice*>& slices);

// S7 编排辅助（flow slice 任务定位子定义网产物）：返回 def 序号 index
// 在树上引用的子定义序号集（block_names = def_paths 序的 block 名清单；
// 树扫描一遍，按 children 节点的 block cell 名对齐、去重升序；定义重名
// 保留首份与 S6 树构建同语义；自引用与未提供定义的引用防御排除）。
CMVector<uint32_t> ds_net_union_child_indexes(
    const DSHierTree& tree, const CMVector<CMString>& block_names,
    uint32_t index);

}  // namespace fly
