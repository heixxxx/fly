#pragma once

// =============================================================================
// S10 汇总校验 + 冻结（方案 design-db-plan.md §3.2 S10 + 2026-09-13 校验
// 分级裁定）。
//
// 校验分级（裁定定稿，红线）：
//   损坏类 → fatal message（码 80 退出 + master 联动，阻断冻结）：
//     并查集不自洽（union 结构错误 = 数据损坏，DSGN::0019）/ 分区网格不
//     无缝覆盖（分区表损坏，DSGN::0020）/ namemap 双向不一致（映射损坏，
//     DSGN::0021）。
//   观测类 → user warn message（不阻断冻结）：
//     global id 连续性——空洞/重复可能丢数据但业务数据本身没问题
//     （DSGN::0022）/ 密度守恒——仅影响分区结果（DSGN::0023）。
//   统计汇总 → INFO message（DSGN::0024）。
//
// 密度守恒 primary 口径（写清口径）：Σ 各分区 INSTANCES primary 实例计
// 数 = Σ 首份定义的 (stats.instance_count − stats.unplaced_count)。右端
// = 树展开可放置实例总数（每个真实实例——leaf、各级 block 实例、fake
// cell 实例——恰一 global id 且恰一 primary 副本；UNPLACED 无物理放置
// 不入分区产物故从期望扣除；root 自身 global 0 无实体副本；非 root 定义
// 的 local 0 保留槽不对应真实实例）。副本不计——extend 副本语义下分区
// 总和必然大于全局。fake cell 实例是真实实例，自然计入两侧。
//
// id 连续性口径（DSIdDomain 三域）：
//   instance  expected = Σ 树节点 instance_count − (非 root 节点数) − 1
//             （扣除保留槽与 root 自身）；actual = 全部分区 INSTANCES
//             distinct id 并集；duplicates = 多 primary 直方图超量计数。
//   net       expected = Σ 树节点 net_count（[0, Σ) 连续）；actual = 全
//             部分区产物覆盖的 distinct 网 id（geometry 含非 OBS 条目的
//             键 + crossing + 两类连接表）；空洞 = 无几何且无连接的空网
//             （合法形态）。
//   via       expected = Σ 树节点 via_count；actual = per-DEF 网产物
//             via_instances_ 键经 (节点 via_start + local − 1) 换算的并
//             集（via instance 不入分区产物，权威存储只在 S5b 产物）。
//
// 任务组织（ds_flow.py，S9 plan 任务动态提交）：每分区一校验任务（并行，
// 只读本分区四类正式产物——红线：不跨区读）→ 全局汇总校验任务（读全部
// 校验结果 + 树 + design + stack + global_density + net_union + per-DEF
// 产物与伴生名；损坏类经 ds_verify_report_or_fatal 逐项 fatal 退出阻断
// 冻结——report 不落盘、freeze 依赖缺失；观测类 warn + 统计 INFO；报告
// 写 "verify_report" 正式对象挂 freeze final_keys——校验未完成不冻结）。
// =============================================================================

#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_flatten.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/design/cpp/ds_union.h>

#include <cstdint>

namespace fly {

class DSBlockBuildData;
class DSBlockNames;
class DSNetBuildData;

// id 连续性观测域（一类 id 的分区产物覆盖 vs 树区间推导期望；口径见文
// 件头注释。空洞/重复均为观测 warn，不构成损坏）
struct DSIdDomain {
    // 树区间推导的应达 id 数（口径随域不同，见文件头）
    uint64_t expected_ = 0;
    // 分区产物实际覆盖的 distinct id 数
    uint64_t actual_ = 0;
    // 空洞数 = expected − actual（下饱和为 0；含 UNPLACED/空网等合法空洞）
    uint64_t holes_ = 0;
    // 重复数 = 多出现超量计数（instance = 多 primary 直方图超量；net 无
    // primary/副本语义恒 0；via 域按 def 产物换算理论恒 0，非 0 即区间
    // 重叠的防御性检测信号——见 ds_verify.cpp via 域注释）
    uint64_t duplicates_ = 0;

    FLY_SERIALIZE(expected_, actual_, holes_, duplicates_)
};

// 分区级校验结果（每分区一任务产出，临时对象经汇总任务合并后即弃）：
// 计数 + 全局校验素材 id 集（id 域判定集中在全局任务——fatal 单点退出）。
class DSPartitionCheckResult {
public:
    uint32_t partition_id_ = 0;
    uint32_t xp_ = 0;
    uint32_t yp_ = 0;
    // primary 实例数（恰一不变式的全局直方图素材）
    uint64_t primary_instance_count_ = 0;
    // 实例副本数（含 primary）
    uint64_t instance_count_ = 0;
    // geometry 出现的网数（键数，含 net 0 桶）
    uint64_t net_count_ = 0;
    // crossing_nets_ 大小
    uint64_t crossing_net_count_ = 0;
    uint64_t geometry_entry_count_ = 0;
    // 两类连接条目总数
    uint64_t connection_count_ = 0;
    // —— 全局校验素材（升序去重；review 2026-09-13 量级声明：id 级
    //    8B/条 ×4 向量，百万级实例副本设计为数十 MB 级临时对象，
    //    freeze 后随 temp 清理释放——当前阶段可接受，后续可区间化）——
    CMVector<uint64_t> primary_instance_ids_;
    CMVector<uint64_t> instance_ids_;
    // 本分区产物覆盖的网 id（geometry 非 OBS 键 + crossing + 两类连接表）
    CMVector<uint64_t> net_ids_;
    CMVector<uint64_t> crossing_net_ids_;

    FLY_SERIALIZE(partition_id_, xp_, yp_, primary_instance_count_,
                  instance_count_, net_count_, crossing_net_count_,
                  geometry_entry_count_, connection_count_,
                  primary_instance_ids_, instance_ids_, net_ids_,
                  crossing_net_ids_)
};

// 全局校验报告（S10 正式对象 "verify_report"，冻结后下游可读）。损坏类
// 字段非空即 fatal（ds_verify_report_or_fatal 逐项处置）；观测类字段随
// warn message 透出，不阻断冻结。
class DSDesignCheckReport {
public:
    // —— 损坏类（非空即 fatal；空串 = 通过）——
    // 并查集两层/双向不自洽（DSGN::0019）
    CMString union_inconsistency_;
    // 分区网格未无缝覆盖（DSGN::0020）
    CMString coverage_gap_;
    // namemap 双向不一致（DSGN::0021）
    CMString namemap_inconsistency_;
    // —— 观测类（warn；空串 = 通过）——
    DSIdDomain instance_ids_;
    DSIdDomain net_ids_;
    DSIdDomain via_ids_;
    // 密度守恒 primary 口径偏差描述（DSGN::0023）
    CMString density_variance_;
    // —— 全局统计（DSGN::0024 INFO 汇总素材）——
    uint64_t expected_instances_ = 0;
    uint64_t expected_nets_ = 0;
    uint64_t expected_vias_ = 0;
    uint64_t total_primary_ = 0;
    uint64_t total_instances_ = 0;
    uint64_t total_nets_ = 0;
    uint64_t total_connections_ = 0;
    uint64_t total_geometry_entries_ = 0;
    uint64_t total_crossing_nets_ = 0;
    // 密度总量（global_density 三通道，与分区计数互为观测参照）
    uint64_t density_instance_total_ = 0;
    uint64_t density_metal_total_ = 0;
    uint64_t density_via_total_ = 0;
    uint32_t partition_count_ = 0;

    FLY_SERIALIZE(union_inconsistency_, coverage_gap_,
                  namemap_inconsistency_, instance_ids_, net_ids_, via_ids_,
                  density_variance_, expected_instances_, expected_nets_,
                  expected_vias_, total_primary_, total_instances_,
                  total_nets_, total_connections_, total_geometry_entries_,
                  total_crossing_nets_, density_instance_total_,
                  density_metal_total_, density_via_total_, partition_count_)
};

// S10 分区级校验（每分区一任务调用；只读本分区四类正式产物——分区对象
// 按类拆写为四对象，签名对齐产物形态而非聚合容器）。
DSPartitionCheckResult ds_verify_partition(
    uint32_t partition_id, uint32_t xp, uint32_t yp,
    const DSPartitionGeometry& geometry,
    const DSPartInstances& instances,
    const DSPartInstConnections& inst_connections,
    const DSPartNetConnections& net_connections);

// 分区网格覆盖校验（损坏类，独立可测）：core 并集对 global_density 域
// 无缝覆盖 = 全部 core 在域内 + 切线对齐格边界 + 两两不重叠（半开，
// 共享边界为正常衔接）+ Σ 面积 = 域面积（矩形面积可加性等价）——
// xp_/yp_ 仅查唯一。区间性质兼容 S8 空段形态（prefix_cuts 切线同值
// 产生零宽段跳过、xp_ 保留原网格坐标——并集覆盖无损，review
// 2026-09-13 修正：原满网格校验误判空段形态为损坏）。
CMString ds_check_partition_coverage(
    const CMVector<DSSubPartition>& partitions,
    const DSDensityGrid& global);

// S10 全局校验（单任务）：树 + 全部分区校验结果 + net_union + DSDesign
// （分区表 + 三 hasher）+ DSStack（layer hasher）+ global_density（覆盖
// 域基准 + 密度总量）+ per-DEF 产物（via id 域与 UNPLACED 计数）+ 伴生名
// （namemap 全查 + 树节点 def 反查）。blocks/nets/names 按 def_paths 序
// 一一对齐（同 ds_build_hier_tree 入参约定）。损坏类写报告字段（调用方
// 经 ds_verify_report_or_fatal 处置），本函数不直接 fatal——纯函数可测。
DSDesignCheckReport ds_verify_design(
    const DSHierTree& tree, const DSDesign& design, const DSStack& stack,
    const DSDensityGrid& global_density, const DSNetUnion& net_union,
    const CMVector<const DSBlockBuildData*>& blocks,
    const CMVector<const DSNetBuildData*>& nets,
    const CMVector<const DSBlockNames*>& names,
    const CMVector<const DSPartitionCheckResult*>& checks);

// 损坏类处置（S10 全局校验任务调用）：报告损坏类字段非空即 MSG_FATAL_EXIT
//（码 80 退出 + master 联动）——逐项顺序判定，首个损坏项触发退出；干净
// 报告正常返回。观测类不在此处置（调用方 warn，不阻断冻结）。
void ds_verify_report_or_fatal(const DSDesignCheckReport& report);

}  // namespace fly
