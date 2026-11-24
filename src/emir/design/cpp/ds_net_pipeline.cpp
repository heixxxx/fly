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
        const CMViaCellId prefixed =
            design.via_cell_names_.get_id(design_name + "::" + via_name);
        if (prefixed.is_valid()) {
            return prefixed;
        }
    }
    // 回退：plain 名（tech/cell lef 来源 via）
    const CMViaCellId plain = design.via_cell_names_.get_id(via_name);
    if (plain.is_valid()) {
        return plain;
    }
    return CMViaCellId{};
}

// —— 节点 1：连接项解析 ——

namespace {

// 同网连接去重集合（2026-11-24 评审修订：连接按集合处理——上游工具
// 实证一个 (实例, 引脚) 在一张网上至多一条连接，显式 + 通配混写、同一
// 通配写两条均去重）。键 = (端点实例 local id, 全局 pin id)，经容器别
// 名层的 tuple hash 组合器（container_aliases.h），零自定义哈希。
using ConnectedPinPairSet = CMUnorderedSet<std::tuple<CMInstanceId, CMPinId>>;

// 显式与通配连接共用的换算（2026-11-24 真实数据兼容裁定：两条路径禁
// 止各持一份逻辑。评审修订：拆两段共用——第一段「实例 → 所属单元」、
// 第二段「引脚存在性 + 标志位 + 端点回填」；显式路径串联两段、port 条
// 目与通配路径各取所需，库内零重复实现）。

// 第一段：实例 → 所属单元（查实例表 + 单元 id 范围检查）。返回 nullptr
// = 占位（local 0）/ fake cell 引用（快照表外），无 pin 可查。
const DSCell* resolve_cell_of_instance(const DSNetContext& ctx,
                                       CMInstanceId inst_local) {
    const auto iit = ctx.block_data->instances_.find(inst_local);
    if (iit == ctx.block_data->instances_.end() ||
        (*iit->second).get_cell_id() >= ctx.design->cells_.size()) {
        return nullptr;
    }
    return &ctx.design->cells_[(*iit->second).get_cell_id().value()];
}

// 第二段：给定单元 + 端点实例 + 引脚 id → 引脚存在性校验 + flags 六位
// 填写 + instance_local_id_ / pin_id_ 回填。返回 false = 引脚 id 未登记
// 或该 cell 定义内无此 pin（裁定 3 存在性校验：全局命中不代表该 cell
// 有此 pin）。cached_flags 非空 = 套用既定标志位位集（通配路径按单元
// 缓存传入——存在性已随缓存判定，免逐实例重扫 pin 表）。
// pin_id → direction/type 现场定位经 cell 内线性遍历：std cell pin 数
// <20 无负担；block cell（port 条目）pin 数当前 10³-10⁴ 级可接受
//（review 注记：port 数上万时可改用 pin id 直等定位）。
bool fill_resolved_connection(DSNetConnection& conn, const DSCell& cell,
                              CMInstanceId inst_local, CMPinId pin_id,
                              const uint8_t* cached_flags = nullptr) {
    if (!pin_id.is_valid()) {
        return false;
    }
    if (cached_flags == nullptr) {
        bool found = false;
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
            found = true;
            break;
        }
        if (!found) {
            return false;
        }
    } else {
        conn.flags_ = *cached_flags;  // 套用按单元缓存的标志位位集
    }
    conn.instance_local_id_ = inst_local;
    conn.pin_id_ = pin_id;
    return true;
}

// 通配条目 ( * 引脚 ) 展开（2026-11-24 真实数据兼容裁定，上游工具读
// DEF 时展开、回写仍用通配简写——库内落展开后的真实连接）：
//   语义 = 本 DEF（块）内每一个「所属单元定义了名为该引脚」的实例各一
//   条连接，与显式 ( 实例 引脚 ) 产生的连接完全相同（同结构、同标志位、
//   同后续路径），对 SPECIALNETS 与 NETS 一视同仁（语法层处理，不按网
//   的类别特判）。细则：
//   - 引脚名 → 全局 pin id 只查一次；「单元是否拥有该引脚」按单元缓存
//     （单元数小；缓存值 = 该引脚的标志位位集，kNoPinFlags 哨兵 = 无此
//     引脚）——位集经第二段探测形态取得，每条展开连接经第二段套用形态
//     产出，查找与回填零内联；
//   - 占位/伪单元实例静默跳过、不报警——通配的本义即「谁有谁连」，无
//     法判定就不连（与显式连接的 DSGN::0025 口径不同，有意差异）；
//   - 引脚名全局未登记 / 无任何实例拥有：零条连接，不报 DSGN::0025，
//     只计通配条目数；
//   - 连接按集合处理（2026-11-24 评审修订，上游实证）：connected_pairs
//     初值 = 本网全部解析成功的显式条目，展开命中集合的跳过、未命中的
//     展开并插入——「显式 + 通配」「通配 + 通配」均不重复；
//   - 遍历顺序 = block 实例表（DSBlockBuildData::instances_）的既有顺
//     序，不引入与运行有关的顺序（同输入两次建库逐字节一致）。
void expand_wildcard_entry(DSNetContext& ctx, const CMString& pin_name,
                           ConnectedPinPairSet& connected_pairs) {
    // 缓存哨兵：-1 = 该单元无此引脚；>= 0 = 该引脚的 flags 位集
    constexpr int8_t kNoPinFlags = -1;
    DSNetStats& stats = ctx.net_data->stats_;
    ++stats.wildcard_entry_count;
    const CMPinId pin_id = ctx.design->pin_names_.get_id(pin_name);
    if (!pin_id.is_valid()) {
        return;  // 全局未登记：零条连接、不报警（只计条目数）
    }
    CMUnorderedMap<CMCellId, int8_t> cell_flag_cache;
    for (const auto& [inst_local, inst] : ctx.block_data->instances_) {
        if (inst_local == CMInstanceId{0}) {
            continue;  // ⑧ 占位（块自身），非真实实例
        }
        const DSCell* cell = resolve_cell_of_instance(ctx, inst_local);
        if (cell == nullptr) {
            continue;  // 伪单元引用（快照表外）：静默跳过不报警
        }
        const CMCellId cell_id = (*inst).get_cell_id();
        auto cached = cell_flag_cache.find(cell_id);
        if (cached == cell_flag_cache.end()) {
            DSNetConnection probe;  // 第二段探测形态：存在性 + 位集一次取得
            const int8_t flags =
                fill_resolved_connection(probe, *cell, inst_local, pin_id)
                    ? static_cast<int8_t>(probe.flags_)
                    : kNoPinFlags;
            cached = cell_flag_cache.emplace(cell_id, flags).first;
        }
        if (cached->second == kNoPinFlags) {
            continue;  // 该单元无此引脚：不连
        }
        // 连接集合去重：显式条目已占位或此前通配已展开的 (实例, 引脚)
        // 不再重复展开
        if (!connected_pairs.insert({inst_local, pin_id}).second) {
            continue;
        }
        const uint8_t flags = static_cast<uint8_t>(cached->second);
        DSNetConnection conn;
        // 套用形态恒成功（位集来自本函数探测形态的既有判定）
        fill_resolved_connection(conn, *cell, inst_local, pin_id, &flags);
        ctx.net_data->add_connection(ctx.local_net_id, std::move(conn));
        ++stats.wildcard_connection_count;
    }
}

}  // namespace

void DSNetConnectionParseNode::handle(DSNetContext& ctx) {
    if (ctx.stack == nullptr || ctx.design == nullptr ||
        ctx.block_data == nullptr || ctx.net_data == nullptr) {
        ctx.error = true;  // 调用方契约错误（环境未挂）
        return;
    }

    // ⑨ local net id 沿用 S5a 网名扫描分配的 local id；未收录（正常数据
    // 流不会发生——S5a NetNameCbk 对全部网名登记，防御兜底）计数跳过。
    // R7 ㊱/㊵②：经 net hasher 查询（block_data 需已注入 DSBlockNames）
    const CMNetId local_id =
        ctx.block_data->net_names_
            ? ctx.block_data->net_names_->get_id(ctx.net_name)
            : CMNetId{};
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
    // 通配预扫：本网存在 ( * 引脚 ) 条目时，构建同网连接去重集合，初值
    // = 本网全部解析成功的显式条目（2026-11-24 评审修订：连接按集合处
    // 理——上游实证一个 (实例, 引脚) 至多一条连接；通配展开命中集合的
    // 跳过、未命中的展开并插入）。无通配条目的网零预扫开销（绝大多数
    // 网）。两条完全相同的显式条目之间的重复为既有行为（照常收录两条
    // 连接），本集合不改变显式条目间的既有语义。
    bool has_wildcard = false;
    for (const DSNetRawConnection& raw : ctx.connections) {
        if (raw.instance_name == "*") {
            has_wildcard = true;
            break;
        }
    }
    ConnectedPinPairSet connected_pairs;
    if (has_wildcard) {
        for (const DSNetRawConnection& raw : ctx.connections) {
            if (raw.instance_name == "*" || raw.instance_name == "PIN") {
                continue;
            }
            const CMInstanceId inst_local =
                ctx.block_data->instance_names_
                    ? ctx.block_data->instance_names_->get_id(
                          raw.instance_name)
                    : CMInstanceId{};
            const CMPinId pin_id =
                ctx.design->pin_names_.get_id(raw.pin_name);
            if (inst_local.is_valid() && pin_id.is_valid()) {
                connected_pairs.insert({inst_local, pin_id});
            }
        }
    }
    for (DSNetRawConnection& raw : ctx.connections) {
        DSNetConnection conn;
        if (raw.instance_name == "*") {
            // 通配条目 ( * 引脚 )：展开为本块内所有拥有该引脚的实例的
            // 连接（与显式写法完全同构；细则见 expand_wildcard_entry）
            expand_wildcard_entry(ctx, raw.pin_name, connected_pairs);
            continue;
        }
        if (raw.instance_name == "PIN") {
            const CMPinId port_pin =
                ctx.design->pin_names_.get_id(raw.pin_name);
            const CMCellId block_cell_id =
                ctx.design->cell_names_.get_id(block_name);
            const bool cell_known = block_cell_id.is_valid() &&
                                    block_cell_id < ctx.design->cells_.size();
            // 共用换算第二段（端点实例 = local 0 占位回填，⑧）：裁定 3
            // 存在性校验——port pin 须在 block cell 定义内
            if (!port_pin.is_valid() || !cell_known ||
                !fill_resolved_connection(
                    conn, ctx.design->cells_[block_cell_id.value()],
                    CMInstanceId{0}, port_pin)) {
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' references unregistered port '{}' — "
                    "connection dropped", ctx.net_name, raw.pin_name);
                continue;
            }
            conn.set_port();
        } else {
            const CMInstanceId inst_local =
                ctx.block_data->instance_names_
                    ? ctx.block_data->instance_names_->get_id(
                          raw.instance_name)
                    : CMInstanceId{};
            if (!inst_local.is_valid()) {
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' references unknown instance '{}' — "
                    "connection dropped", ctx.net_name, raw.instance_name);
                continue;
            }
            // 共用换算串联两段（显式/通配同一逻辑）：第一段定位所属单元
            // ——占位（local 0）或 fake cell 引用（快照表外）= 无单元；
            // 第二段做引脚存在性校验（裁定 3：全局名字命中 ≠ 该 cell 有
            // 此 pin）+ 标志位 + 端点回填。显式路径逐项 DSGN::0025 +
            // 计数（通配路径静默跳过，差异见 expand_wildcard_entry 注释）
            const CMPinId pin_id =
                ctx.design->pin_names_.get_id(raw.pin_name);
            const DSCell* cell = resolve_cell_of_instance(ctx, inst_local);
            if (cell == nullptr) {
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' instance '{}' has no resolvable cell "
                    "(placeholder/fake, pin '{}' unreachable) "
                    "— connection dropped", ctx.net_name, raw.instance_name,
                    raw.pin_name);
                continue;
            }
            if (!fill_resolved_connection(conn, *cell, inst_local, pin_id)) {
                ++ctx.net_data->stats_.skipped_invalid_connection_count;
                MSG("DSGN::0025", 0,
                    "net '{}' references undefined pin '{}' of cell '{}' — "
                    "connection dropped", ctx.net_name, raw.pin_name,
                    cell->name_);
                continue;
            }
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
