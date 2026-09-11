#pragma once

// =============================================================================
// S5a COMPONENTS 责任链（㉛：DEF 解析业务处理组织为处理链，方案
// design-db-phase2-plan.md §2.1/§2.2）——一份 instance 数据顺序流经链
// 节点，每节点处理相应部分并收集信息；新功能 = 在链中登记新节点，不改
// 既有节点。
//
// 链组织：
//   DSInstanceContext   链上传递的可变上下文（输入数据 + 环境引用 + 节点
//                       产出 + 错误标记）
//   DSInstanceHandler   抽象节点（name + handle）
//   DSInstancePipeline  固定顺序执行，节点置 error 即停
//
// S5a 四节点（首批）：
//   DSCellResolveNode   master 名查 cell namemap；未定义 → fake cell
//                       生成（⑲/⑳），ctx 填 cell id 与放置换算所需的
//                       cell 几何（bbox/origin 值拷贝，防 vector 重分配
//                       失效）；
//   DSInstanceBuildNode local instance id 从 1 起分配（⑧ per-DEF 计数
//                       器）、place_from_def 换算 transform_（R6）、
//                       status/weight 填充、实例入表；
//   DSDensityNode       实例 footprint（cell bbox 经 transform_）与固定
//                       采样格子的交叠计数（实例面积通道，⑥ 图形计数
//                       口径）；UNPLACED 不计（D14）；
//   DSStatsNode         per-cell 引用计数、fake cell 计数、UNPLACED
//                       计数等统计。
//
// 并行轨（同一 per-DEF 任务、同一遍 DEF 读取）：NETS/SPECIALNETS 网名
// 扫描（③ NetNameOnly，适配层 defrSetNetNameCbk + SkipNetDetails/
// SkipSNetDetails 实现，见 ds_def_adapter.h）。
//
// fake cell id 算法（⑳「max_cell_id + 简易唯一值」的实施版）：
//   fake_id = max_cell_id + 1 + fnv1a_32(block 名) % 65536 + def 内递增序号
//   - max_cell_id = 全局正常 cell 总数（S5a 启动时正常 cell 均已创建
//     完毕、id 已固定，各并行任务取同一 DSDesign 快照 → 基址一致）；
//   - hash 高位扰动使不同 DEF 任务落在不同基址（跨任务冲突 = hash 同
//     桶且序号重叠，概率小；⑳ 裁定冲突率不必严格）；
//   - 冲突兜底：汇总任务（ds_merge_block_build）对已占用 id 顺延 + 引用
//     重映射，正确性不依赖「无冲突」。
//   扰动空间 65536 同时是汇总稀疏落位的空洞预算上界（占位 cell 内存
//   可忽略）。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <emir/design/cpp/ds_types.h>
#include <geometry/cpp/geometry_types.h>
#include <geometry/cpp/transform.h>

#include <memory>

namespace fly {

// 链上传递的可变上下文：一行 COMPONENTS 数据 + 环境观察引用 + 节点产出。
// 环境引用为非拥有观察（裸指针仅限非拥有观察，见 docs/
// DEVELOPMENT_GUIDELINES.md §16）；生命周期由调用方保证覆盖 run。
struct DSInstanceContext {
    // —— 输入（COMPONENTS 一行，坐标已换算全局 DBU；适配层填充）——
    // 实例名（DEF instName）
    CMString instance_name;
    // master cell 名（DEF modelName）
    CMString master_name;
    // 当前 block 名（DESIGN 名；fake cell 命名前缀来源）
    CMString block_name;
    // DEF placement 点 t（全局 DBU）
    GEOPoint placement{0, 0};
    // 放置朝向（defin 回调整型直转；UNPLACED 的 defi 无效值 −1 由适配
    // 层钳制为 N）
    GEOOrientation orient = GEOOrientation::N;
    // 放置状态（DSPlacementStatus）
    uint8_t placement_status = static_cast<uint8_t>(DSPlacementStatus::UNPLACED);
    // OPTIONAL weight（DEF 未给 = 0）
    double weight = 0.0;

    // —— 环境（非拥有观察；调用方保证覆盖 pipeline.run 生命周期）——
    // 全局容器（cell namemap 查询；fake cell 不直接写入——入 per-DEF
    // 产物，汇总任务并入）
    const DSDesign* design = nullptr;
    // per-DEF 产物容器（实例表 / 密度 / 统计 / fake 登记）
    DSBlockBuildData* block_data = nullptr;

    // —— 节点产出 ——
    // DSCellResolveNode：解析到的 cell id（fake 为任务内分配 id）
    uint32_t cell_id = DSDesign::kInvalidId;
    // DSCellResolveNode：放置换算所需的 cell 几何（bbox/origin 值拷贝
    // —— fake cell 不在 design 表，且 CMVector push 可能重分配）
    GEORect cell_bbox{0, 0, 0, 0};
    int32_t cell_origin_x = 0;
    int32_t cell_origin_y = 0;
    // DSInstanceBuildNode：分配到的 local instance id（R7 ㊳ 64 位）
    uint64_t instance_id = 0;

    // 错误标记：任一节点置位后 run 即停（环境缺失等调用方契约错误）
    bool error = false;
};

// 抽象链节点
class DSInstanceHandler {
public:
    virtual ~DSInstanceHandler() = default;
    // 节点名（日志/诊断用）
    virtual const char* name() const = 0;
    // 处理一份 instance 上下文（就地修改收集）
    virtual void handle(DSInstanceContext& ctx) = 0;
};

// 链组织：固定顺序执行；节点置 error 后停止
class DSInstancePipeline {
public:
    // 登记节点（链序 = 登记序；节点所有权归管道）
    void add(CMUniquePtr<DSInstanceHandler> handler);
    size_t handler_count() const { return handlers_.size(); }
    // 顺序执行（可重复 run：一次构建多次使用）；ctx.error 置位即停
    void run(DSInstanceContext& ctx) const;

private:
    CMVector<CMUniquePtr<DSInstanceHandler>> handlers_;
};

// —— S5a 四节点（首批）———————————————————————————————

// 节点 1：master cell 名解析（namemap 命中 / fake cell 生成，⑲/⑳）
class DSCellResolveNode : public DSInstanceHandler {
public:
    const char* name() const override { return "CellResolveNode"; }
    void handle(DSInstanceContext& ctx) override;
};

// 节点 2：local instance 生成（id 从 1 起 + place_from_def 换算 + 入表）
class DSInstanceBuildNode : public DSInstanceHandler {
public:
    const char* name() const override { return "InstanceBuildNode"; }
    void handle(DSInstanceContext& ctx) override;
};

// 节点 3：实例面积密度通道（footprint 与固定采样格子交叠计数；UNPLACED
// 不计）
class DSDensityNode : public DSInstanceHandler {
public:
    const char* name() const override { return "DensityNode"; }
    void handle(DSInstanceContext& ctx) override;
};

// 节点 4：统计收集（per-cell 引用计数 / fake / UNPLACED）
class DSStatsNode : public DSInstanceHandler {
public:
    const char* name() const override { return "StatsNode"; }
    void handle(DSInstanceContext& ctx) override;
};

// S5a 默认链装配（四节点按序）；导出面供 Python 编排取用
DSInstancePipeline ds_make_components_pipeline();

// fake cell id 基址（算法见头注释）：max_cell_id + 1 + hash(block 名) 扰动
uint32_t ds_fake_cell_id_base(const CMString& block_name,
                              uint32_t max_cell_id);

}  // namespace fly
