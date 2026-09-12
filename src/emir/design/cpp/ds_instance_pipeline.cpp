#include <emir/design/cpp/ds_instance_pipeline.h>

#include <emir/design/cpp/ds_transform_util.h>
#include <message/cpp/message_macros.h>

#include <utility>

namespace fly {

// —— DSInstancePipeline ——

void DSInstancePipeline::add(CMUniquePtr<DSInstanceHandler> handler) {
    handlers_.push_back(std::move(handler));
}

void DSInstancePipeline::run(DSInstanceContext& ctx) const {
    for (const auto& handler : handlers_) {
        if (ctx.error) {
            break;  // 错误即停（调用方契约错误由前置节点标记）
        }
        handler->handle(ctx);
    }
}

// —— fake cell id 算法（⑳；算法说明见头注释）——

uint32_t ds_fake_cell_id_base(const CMString& block_name,
                              uint32_t max_cell_id) {
    // FNV-1a 32 位（确定性、跨平台一致）
    uint32_t hash = 2166136261u;
    for (const char c : block_name) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    // 扰动空间 65536：跨任务基址分散 + 汇总稀疏落位空洞预算上界
    return max_cell_id + 1 + (hash % 65536u);
}

// —— 节点 1：master cell 解析（namemap / fake cell 生成）——

void DSCellResolveNode::handle(DSInstanceContext& ctx) {
    if (ctx.design == nullptr || ctx.block_data == nullptr) {
        ctx.error = true;  // 调用方契约错误（环境未挂）
        return;
    }

    // 1) 正常 cell（S2 macro + S4 block cell 同空间 cell hasher）
    const uint32_t resolved = ctx.design->cell_names_.get_id(ctx.master_name);
    if (DSCellNameHasher::is_valid_id(resolved)) {
        ctx.cell_id = resolved;
        const DSCell& cell = ctx.design->cells_[ctx.cell_id];
        ctx.cell_bbox = cell.get_bbox();
        ctx.cell_origin_x = cell.get_origin_x();
        ctx.cell_origin_y = cell.get_origin_y();
        return;
    }

    // 2) 本 DEF 已生成的 fake（同 master 复用同一 fake cell；登记键 =
    //    完整 fake 名 block::master）
    const CMString fake_key = ctx.block_name + "::" + ctx.master_name;
    auto fit = ctx.block_data->fake_name_to_id_.find(fake_key);
    if (fit != ctx.block_data->fake_name_to_id_.end()) {
        ctx.cell_id = fit->second;
        ctx.cell_bbox = GEORect(0, 0, 1, 1);
        ctx.cell_origin_x = 0;
        ctx.cell_origin_y = 0;
        return;
    }

    // 3) fake cell 生成（⑲/⑳）：名 = block::cell（同 ⑫ via 前缀模式）、
    //    1×1 全局最小单位矩形、无 pin、fake_cell 位；不 raise 不跳过 +
    //    DSGN::0007 提醒（实例清单保持完整）
    DSCell fake;
    fake.set_name(fake_key);
    fake.set_bbox(GEORect(0, 0, 1, 1));
    fake.set_fake_cell();

    const uint32_t fake_id =
        ds_fake_cell_id_base(ctx.block_name,
                             static_cast<uint32_t>(ctx.design->cells_.size())) +
        static_cast<uint32_t>(ctx.block_data->fake_cells_.size());
    ctx.block_data->fake_name_to_id_[fake.get_name()] = fake_id;
    ctx.block_data->fake_cells_.push_back(std::move(fake));
    ++ctx.block_data->stats_.fake_cell_count;

    ctx.cell_id = fake_id;
    ctx.cell_bbox = GEORect(0, 0, 1, 1);
    ctx.cell_origin_x = 0;
    ctx.cell_origin_y = 0;
    MSG("DSGN::0007", 0, "undefined cell '{}' fake cell created",
        ctx.master_name);
}

// —— 节点 2：local instance 生成 ——

void DSInstanceBuildNode::handle(DSInstanceContext& ctx) {
    if (ctx.block_data == nullptr ||
        ctx.cell_id == DSDesign::kInvalidId) {
        ctx.error = true;
        return;
    }
    DSInstance inst;
    inst.set_cell_id(ctx.cell_id);
    // R6：t → pos 放置边界换算（orient 直转已在适配层完成）
    inst.set_transform(place_from_def(ctx.placement,
                                      static_cast<int>(ctx.orient),
                                      ctx.cell_bbox, ctx.cell_origin_x,
                                      ctx.cell_origin_y));
    inst.set_placement_status(ctx.placement_status);
    inst.set_weight(ctx.weight);
    // R7 ㊱：实例名不进 DSInstance——登记进双向 instance hasher
    ctx.instance_id =
        ctx.block_data->add_instance(std::move(inst), ctx.instance_name);
}

// —— 节点 3：实例面积密度通道 ——

void DSDensityNode::handle(DSInstanceContext& ctx) {
    if (ctx.block_data == nullptr) {
        ctx.error = true;
        return;
    }
    // UNPLACED 不计（D14：无坐标无法入分区）
    if (ctx.placement_status ==
        static_cast<uint8_t>(DSPlacementStatus::UNPLACED)) {
        return;
    }
    // block instance 自身 bbox 不计（2026-09-12 裁定 5，S8 前置修正）：
    // block 的密度贡献 = S8 合并时子块实例/网密度图按放置平移撒入，此处
    // 再计 block footprint 会双计。判定 = cell 的 block_cell 位（cell 查
    // 表经 design.cells_；fake cell 不在表内 → 恒非 block）
    if (ctx.design != nullptr && ctx.cell_id < ctx.design->cells_.size() &&
        ctx.design->cells_[ctx.cell_id].is_block_cell()) {
        return;
    }
    const DSInstance* inst = ctx.block_data->find_instance(ctx.instance_id);
    if (inst == nullptr) {
        ctx.error = true;
        return;
    }
    // instance footprint = cell bbox 经 instance transform_
    ctx.block_data->density_.accumulate_footprint(
        inst->get_transform().apply_box(ctx.cell_bbox));
}

// —— 节点 4：统计收集 ——

void DSStatsNode::handle(DSInstanceContext& ctx) {
    if (ctx.block_data == nullptr) {
        ctx.error = true;
        return;
    }
    DSInstanceStats& stats = ctx.block_data->stats_;
    ++stats.instance_count;
    if (ctx.placement_status ==
        static_cast<uint8_t>(DSPlacementStatus::UNPLACED)) {
        ++stats.unplaced_count;
    }
    ++stats.per_cell_counts_[ctx.cell_id];
}

// —— S5a 默认链 ——

DSInstancePipeline ds_make_components_pipeline() {
    DSInstancePipeline pipeline;
    pipeline.add(std::make_unique<DSCellResolveNode>());
    pipeline.add(std::make_unique<DSInstanceBuildNode>());
    pipeline.add(std::make_unique<DSDensityNode>());
    pipeline.add(std::make_unique<DSStatsNode>());
    return pipeline;
}

}  // namespace fly
