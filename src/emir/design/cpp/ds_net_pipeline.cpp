#include <emir/design/cpp/ds_net_pipeline.h>

#include <message/cpp/message_macros.h>

#include <algorithm>
#include <utility>

namespace fly {

// —— DSNetPipeline ——

void DSNetPipeline::add(CMUniquePtr<DSNetHandler> handler) {
    handlers_.push_back(std::move(handler));
}

void DSNetPipeline::run(DSNetContext& ctx) const {
    for (const auto& handler : handlers_) {
        if (ctx.error) {
            break;  // 错误即停（调用方契约错误由前置节点标记）
        }
        handler->handle(ctx);
    }
}

// —— via 名解析（⑪ 权威表 + ⑫ 前缀优先）——

CMViaCellId ds_resolve_via_cell(const DSDesign& design,
                                const CMString& design_name,
                                const CMString& via_name) {
    // ⑫ 前缀名优先：DEF 网内引用应优先命中本 DEF VIAS 段登记的 via
    //（design_name::via_name）——tech/cell lef 的同名 via（形状可能不同，
    // ⑫ 正为防此合并污染而独立登记）不得遮蔽本 DEF 定义
    if (!design_name.empty()) {
        const CMViaCellId prefixed{
            design.via_cell_names_.get_id(design_name + "::" + via_name)};
        if (prefixed.is_valid()) {
            return prefixed;
        }
    }
    // 回退：plain 名（tech/cell lef 来源 via）
    const CMViaCellId plain{design.via_cell_names_.get_id(via_name)};
    if (plain.is_valid()) {
        return plain;
    }
    return CMViaCellId{};
}

// —— 节点 1：连接项解析 ——

void DSNetConnectionParseNode::handle(DSNetContext& ctx) {
    if (ctx.stack == nullptr || ctx.design == nullptr ||
        ctx.block_data == nullptr || ctx.net_data == nullptr) {
        ctx.error = true;  // 调用方契约错误（环境未挂）
        return;
    }

    // ⑨ local net id 沿用 S5a 网名扫描分配的 local id；未收录（正常数据
    // 流不会发生——S5a NetNameCbk 对全部网名登记，防御兜底）计数跳过。
    // R7 ㊱/㊵②：经 net hasher 查询（block_data 需已注入 DSBlockNames）
    const CMNetId local_id{
        ctx.block_data->net_names_
            ? ctx.block_data->net_names_->get_id(ctx.net_name)
            : 0};
    if (!local_id.is_valid()) {
        ++ctx.net_data->stats_.skipped_net_count;
        return;  // local_net_id 保持 0，后续节点跳过
    }
    ctx.local_net_id = local_id;
    ++ctx.net_data->stats_.net_count;
    // pg 判定记录（S9 分侧口径：pg 网 NETS_PG 对象不全量补全）
    if (ctx.is_pg) {
        ctx.net_data->mark_pg_net(local_id);
    }
    // 网 USE 收录（2026-09-13 全量补收裁定：随 local id 对齐记录，非
    // SIGNAL 才落存储、缺省读取 SIGNAL——record_net_use 内部判别）
    ctx.net_data->record_net_use(local_id, ctx.use);

    // 连接项 id 换算（2026-09-13 裁定：解析边界一次完成，S7/S9 内部链路
    // 零字符串匹配；2026-09-16 裁定 3：pin 查询 = 裸 pin 名直查全局名
    // 字空间）：
    //   instance 条目：实例名 → local id（S5a 实例 hasher，任务输入已注
    //   入）→ 所属 cell → pin 名查容器 pin hasher 得全局 id——**并校验
    //   所属 cell 定义内确有此 pin**（全局命中不代表该 cell 有此 pin，
    //   裁定 3 后 id 空间为库级 pin 名集）；
    //   "PIN" port 条目：port 名查全局名字空间 + block cell 定义校验
    //  （defi 回调语义 ( PIN portName ) 移植为 port 位 + local 0 占位）。
    // flags 六位（port/driver/receiver/power/ground/clock）换 id 时一并
    // 填写：定位到的 DSPin 的 direction/type 顺手取得（cell 内 pin 无名
    // R7——pin_id 匹配遍历，cell pin 数量级小）。
    // 未命中（实例名未登记 / cell 无此 pin / port 未注册）→ 兜底跳过 +
    // skipped_invalid_connection_count 计数 + DSGN::0025 提醒（dev-rules
    // §7 不 raise；对齐 fake cell/未定义 via 的先例语义）
    const CMString& block_name = ctx.block_data->get_block_name();
    // pin_id → direction/type 匹配经 cell 内线性遍历：std cell pin 数
    // <20 无负担；block cell（port 条目）pin 数当前 10³-10⁴ 级可接受
    //（review 注记：port 数上万时可改用 pin id 直等定位）。返回 false =
    // 该 cell 定义内无此 pin id（裁定 3 存在性校验）
    const auto fill_pin_flags = [](DSNetConnection& conn, const DSCell& cell,
                                   CMPinId pin_id) -> bool {
        for (const DSPin& p : cell.pins_) {
            if (p.pin_id_ != pin_id) {
                continue;
            }
            const DSPinDirection dir = p.direction_;
            if (dir == DSPinDirection::OUTPUT ||
                dir == DSPinDirection::INOUT) {
                conn.set_driver();
            }
            if (dir == DSPinDirection::INPUT ||
                dir == DSPinDirection::INOUT) {
                conn.set_receiver();  // INOUT = driver+receiver 同置 = hybrid
            }
            const DSPinType type = p.type_;
            if (type == DSPinType::POWER) {
                conn.set_power();
            } else if (type == DSPinType::GROUND) {
                conn.set_ground();
            } else if (type == DSPinType::CLOCK) {
                conn.set_clock();
            }
            return true;
        }
        return false;
    };
    for (DSNetRawConnection& raw : ctx.connections) {
        DSNetConnection conn;
        if (raw.instance_name == "PIN") {
            const CMPinId port_pin{
                ctx.design->pin_names_.get_id(raw.pin_name)};
            const CMCellId block_cell_id{
                ctx.design->cell_names_.get_id(block_name)};
            const bool cell_known = block_cell_id.is_valid() &&
                                    block_cell_id < ctx.design->cells_.size();
            // 裁定 3 存在性校验：port pin 须在 block cell 定义内
            if (!port_pin.is_valid() || !cell_known ||
                !fill_pin_flags(
                    conn, ctx.design->cells_[block_cell_id.value()],
                    port_pin)) {
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' references unregistered port '{}' — "
                    "connection dropped", ctx.net_name, raw.pin_name);
                continue;
            }
            conn.pin_id_ = port_pin;
            conn.set_port();
        } else {
            const CMInstanceId inst_local{
                ctx.block_data->instance_names_
                    ? ctx.block_data->instance_names_->get_id(
                          raw.instance_name)
                    : CMInstanceId::kInvalid};
            if (!inst_local.is_valid()) {
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' references unknown instance '{}' — "
                    "connection dropped", ctx.net_name, raw.instance_name);
                continue;
            }
            const auto iit = ctx.block_data->instances_.find(inst_local);
            if (iit == ctx.block_data->instances_.end() ||
                (*iit->second).get_cell_id() >= ctx.design->cells_.size()) {
                // 占位（local 0）或 fake cell 引用（快照表外）：无 pin 可查
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' instance '{}' has no resolvable cell "
                    "(placeholder/fake, pin '{}' unreachable) "
                    "— connection dropped", ctx.net_name, raw.instance_name,
                    raw.pin_name);
                continue;
            }
            const DSCell& cell =
                ctx.design->cells_[(*iit->second).get_cell_id().value()];
            const CMPinId pin_id{
                ctx.design->pin_names_.get_id(raw.pin_name)};
            // 裁定 3 存在性校验：全局名字命中 ≠ 该 cell 有此 pin
            if (!pin_id.is_valid() || !fill_pin_flags(conn, cell, pin_id)) {
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' references undefined pin '{}' of cell '{}' — "
                    "connection dropped", ctx.net_name, raw.pin_name,
                    cell.name_);
                continue;
            }
            conn.instance_local_id_ = inst_local;
            conn.pin_id_ = pin_id;
        }
        ctx.net_data->add_connection(ctx.local_net_id, std::move(conn));
    }
}

// —— 节点 2：路由几何展开 ——

void DSNetGeometryExpandNode::handle(DSNetContext& ctx) {
    if (ctx.net_data == nullptr) {
        ctx.error = true;
        return;
    }
    if (ctx.local_net_id == CMNetId{0}) {
        return;  // 网名未收录（节点 1 兜底计数），跳过
    }
    const CMNetId net_id = ctx.local_net_id;

    // wire 段：layer id 解析（层引用未定义 → 该 wire 段条目级丢弃 +
    // DSGN::0010 提醒 + 计数，dev-rules §7 不 raise）+ 宽度（special
    // 显式保留 / 普通 net 回填 stack 层缺省宽）
    for (DSNetRawWire& raw : ctx.wires) {
        const CMLayerId layer_id =
            ds_resolve_layer_id(raw.layer_name, *ctx.stack);
        if (!layer_id.is_valid()) {
            ++ctx.net_data->stats_.skipped_layer_ref_count;
            continue;
        }
        DSNetWire wire;
        wire.layer_id_ = layer_id;
        wire.width_ = raw.width_dbu > 0
                          ? raw.width_dbu
                          : ctx.stack->layer_by_id(layer_id).get_default_width();
        wire.points_ = std::move(raw.points);
        ctx.net_data->add_wire(net_id, std::move(wire));
    }

    // rect 项：layer id 解析（未定义层 → 条目级丢弃 + 计数，同 wire 段
    // 口径）+ 原样收录
    for (DSNetRawRect& raw : ctx.rects) {
        const CMLayerId layer_id =
            ds_resolve_layer_id(raw.layer_name, *ctx.stack);
        if (!layer_id.is_valid()) {
            ++ctx.net_data->stats_.skipped_layer_ref_count;
            continue;
        }
        DSNetRect rect;
        rect.layer_id_ = layer_id;
        rect.rect_ = raw.rect;
        ctx.net_data->add_rect(net_id, std::move(rect));
    }

    // via 引用：命名解析（plain → ⑫ 前缀回退）+ VIADATA 阵列展开
    //（⑩ via instance 专用 id 空间从 1 起、无 name；未定义 via 跳过 +
    // 计数，DSGN::0008 数据源，不 raise 不拦截）
    for (DSNetRawVia& raw : ctx.vias) {
        const CMViaCellId via_cell_id =
            ds_resolve_via_cell(*ctx.design, ctx.design_name, raw.via_name);
        if (!via_cell_id.is_valid()) {
            ++ctx.net_data->stats_.skipped_via_count;
            MSG("DSGN::0008", 0, "undefined via reference '{}' in net '{}'",
                raw.via_name, ctx.net_name);
            continue;
        }
        const int32_t nx = std::max<int32_t>(raw.num_x, 1);
        const int32_t ny = std::max<int32_t>(raw.num_y, 1);
        for (int32_t iy = 0; iy < ny; ++iy) {
            for (int32_t ix = 0; ix < nx; ++ix) {
                DSViaInstance inst;
                inst.via_cell_id_ = via_cell_id;
                inst.pos_ = GEOPoint(
                    raw.x + ix * raw.step_x, raw.y + iy * raw.step_y);
                ctx.net_data->add_via_instance(net_id, std::move(inst));
            }
        }
    }
}

// —— 节点 3：金属/通孔计数密度通道（⑥ 逐层分列）——

void DSNetDensityNode::handle(DSNetContext& ctx) {
    if (ctx.net_data == nullptr) {
        ctx.error = true;
        return;
    }
    if (ctx.local_net_id == CMNetId{0}) {
        return;  // 网名未收录，跳过
    }
    DSDensityGrid& density = ctx.net_data->density_;

    // 金属通道：wire 段按相邻点对展开为段矩形（宽度向两侧各扩 width/2，
    // int64 中间量）；rect 项原样计入
    const auto wires = ctx.net_data->wires_of(ctx.local_net_id).lock();
    if (wires != nullptr) {
        for (const DSNetWire& wire : *wires) {
            const int64_t half_w = wire.width_ / 2;
            for (size_t i = 0; i + 1 < wire.points_.size(); ++i) {
                const GEOPoint& p0 = wire.points_[i];
                const GEOPoint& p1 = wire.points_[i + 1];
                const int64_t x_lo =
                    std::min<int64_t>(p0.get_x(), p1.get_x()) - half_w;
                const int64_t y_lo =
                    std::min<int64_t>(p0.get_y(), p1.get_y()) - half_w;
                const int64_t x_hi =
                    std::max<int64_t>(p0.get_x(), p1.get_x()) + half_w;
                const int64_t y_hi =
                    std::max<int64_t>(p0.get_y(), p1.get_y()) + half_w;
                density.accumulate_layer_shape(
                    wire.layer_id_, false,
                    GEORect(static_cast<int32_t>(x_lo),
                                     static_cast<int32_t>(y_lo),
                                     static_cast<int32_t>(x_hi),
                                     static_cast<int32_t>(y_hi)));
            }
        }
    }
    const auto rects = ctx.net_data->rects_of(ctx.local_net_id).lock();
    if (rects != nullptr) {
        for (const DSNetRect& rect : *rects) {
            density.accumulate_layer_shape(rect.layer_id_, false,
                                           rect.rect_);
        }
    }

    // 通孔通道：via instance 处 via cell 的 cut 图形平移（分层键 = via
    // cell 的 cut 层 id；未判定兜底回退 bottom 层）
    const auto via_ids = ctx.net_data->via_ids_of(ctx.local_net_id).lock();
    if (via_ids != nullptr) {
        for (const CMViaInstanceId via_id : *via_ids) {
            const auto inst = ctx.net_data->via_instance_at(via_id).lock();
            if (inst == nullptr ||
                inst->via_cell_id_ >= ctx.design->via_cells_.size()) {
                continue;  // 防御（权威表快照外 id）
            }
            const DSViaCell& via =
                ctx.design->via_cells_[inst->via_cell_id_.value()];
            const CMLayerId cut_layer =
                via.get_cut_layer_id().is_valid() ? via.get_cut_layer_id()
                                                  : via.get_bottom_layer_id();
            for (uint32_t k = 0; k < via.cut_rect_count(); ++k) {
                const GEORect& r = via.cut_rect_at(k);
                density.accumulate_layer_shape(
                    cut_layer, true,
                    GEORect(
                        r.get_x_low() + inst->pos_.get_x(),
                        r.get_y_low() + inst->pos_.get_y(),
                        r.get_x_high() + inst->pos_.get_x(),
                        r.get_y_high() + inst->pos_.get_y()));
            }
        }
    }
}

// —— S5b 默认链 ——

DSNetPipeline ds_make_nets_pipeline() {
    DSNetPipeline pipeline;
    pipeline.add(std::make_unique<DSNetConnectionParseNode>());
    pipeline.add(std::make_unique<DSNetGeometryExpandNode>());
    pipeline.add(std::make_unique<DSNetDensityNode>());
    return pipeline;
}

}  // namespace fly
