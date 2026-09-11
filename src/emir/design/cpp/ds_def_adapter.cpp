#include <emir/design/cpp/ds_def_adapter.h>

#include <lefdef/def/def/defrReader.hpp>

#include <emir/design/cpp/ds_instance_pipeline.h>
#include <emir/design/cpp/ds_net_pipeline.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <utility>

namespace fly {

namespace {

// DEF 坐标（DBU_def）→ 全局 DBU：v × stack.dbu_per_micron / def_units，
// 四舍五入（与 T4 换算规则一致）
int64_t def_to_dbu(int64_t v, int32_t stack_dbu, int64_t def_units) {
    return static_cast<int64_t>(std::llround(
        static_cast<double>(v) * static_cast<double>(stack_dbu) /
        static_cast<double>(def_units)));
}

GEORect def_rect_to_dbu(int64_t xl, int64_t yl, int64_t xh, int64_t yh,
                                int32_t stack_dbu, int64_t def_units) {
    return GEORect(
        static_cast<int32_t>(def_to_dbu(xl, stack_dbu, def_units)),
        static_cast<int32_t>(def_to_dbu(yl, stack_dbu, def_units)),
        static_cast<int32_t>(def_to_dbu(xh, stack_dbu, def_units)),
        static_cast<int32_t>(def_to_dbu(yh, stack_dbu, def_units)));
}

uint32_t require_layer_id(const char* name, const DSStack& stack) {
    const uint32_t id = stack.find_layer(name);
    if (id == DSStack::kNoLayer) {
        // 层引用未定义属格式错误（D15）——raise
        throw std::runtime_error(std::string("ds_def_adapter: undefined "
                                             "layer reference '") +
                                 name + "'");
    }
    return id;
}

uint8_t map_port_use(const char* use) {
    if (std::strcmp(use, "POWER") == 0) {
        return static_cast<uint8_t>(DSPinType::POWER);
    }
    if (std::strcmp(use, "GROUND") == 0) {
        return static_cast<uint8_t>(DSPinType::GROUND);
    }
    return static_cast<uint8_t>(DSPinType::SIGNAL);
}

uint8_t map_port_direction(const char* dir) {
    if (std::strcmp(dir, "OUTPUT") == 0 || std::strcmp(dir, "TRISTATE") == 0) {
        return static_cast<uint8_t>(DSPinDirection::OUTPUT);
    }
    if (std::strcmp(dir, "INOUT") == 0 || std::strcmp(dir, "FEEDTHRU") == 0) {
        return static_cast<uint8_t>(DSPinDirection::INOUT);
    }
    return static_cast<uint8_t>(DSPinDirection::INPUT);
}

// —— 回调上下文（经 defrRead userData 传递，无全局状态）——
struct DefContext {
    const DSStack* stack;
    CMVector<DSCell>* block_cells;
    DSPinGeometry* port_geoms;
    CMVector<DSViaCell>* vias;
    DSDefParseStats* stats;
    // UNITS 回调（DEF 单行式，DISTANCE MICRONS N）。DEF 规范 UNITS 语句
    // 可选、缺省 100 database units per micron——初始即缺省值，防无 UNITS
    // 语句的文件在 DIEAREA/PINS 换算时除零。
    int64_t def_units = 100;
    CMString design_name;   // DESIGN 回调（标准 DEF 语法位于 VIAS 之前）
    CMString def_path;      // 来源文件路径（def_path_ 可追溯，dev-rules §7）
    DSCell current_cell;    // DESIGN 建立后累积，DesignEnd 收录（㉙ block cell）
    bool block_active = false;
    // R7 ㊱：port pin 名序列（DSPin 不存 name）——与 current_cell.pins_
    // 下标对齐，DesignEnd 收录 block cell 时随产物流转（汇总
    // ds_merge_def_header 经此进全局 pin hasher）。重名 port 查重亦经此。
    CMVector<CMString> port_names;
};

// ⑫：via 登记名 = design_name::via_name（DESIGN 名缺省时降级为原名，
// 标准 DEF 语法 DESIGN 先于 VIAS，此为非常规文件兜底）
CMString prefixed_via_name(const DefContext* ctx, const char* name) {
    if (ctx->design_name.empty()) {
        return CMString(name);
    }
    return ctx->design_name + "::" + name;
}

// 重名保留首份（⑫：同 DEF 内重名登记为非法重复，DSGN::0005 数据源）
bool add_via_unique(CMVector<DSViaCell>& vias, DSViaCell&& cell,
                    DSDefParseStats& stats) {
    for (const auto& v : vias) {
        if (v.get_name() == cell.get_name()) {
            ++stats.via_conflict_count;
            return false;
        }
    }
    vias.push_back(std::move(cell));
    return true;
}

int def_design_cbk(defrCallbackType_e, const char* name, defiUserData ud) {
    auto* ctx = static_cast<DefContext*>(ud);
    ctx->design_name = name;
    // ㉙：block cell 直接以 DSCell 承载（block_cell 位标记）
    ctx->current_cell = DSCell{};
    ctx->current_cell.set_name(name);
    ctx->current_cell.set_class("BLOCK");
    ctx->current_cell.set_block_cell();
    ctx->current_cell.set_def_path(ctx->def_path);
    ctx->block_active = true;
    return 0;
}

int def_units_cbk(defrCallbackType_e, double value, defiUserData ud) {
    auto* ctx = static_cast<DefContext*>(ud);
    ctx->def_units = static_cast<int64_t>(value);
    ctx->current_cell.set_def_units_per_micron(
        static_cast<int32_t>(ctx->def_units));
    return 0;
}

// DIEAREA（㉞ 双存）：T2 红线——defiBox 的 xl/yl/xh/yh 是「前两点」向后
// 兼容赋值，必须经 getPoint() 取完整点集。>2 点 = 轴对齐多边形（polygon
// 全点集 + is_polygon 置位）；2 点 = 矩形（polygon 空复位）。bbox 恒存
// （聚合包围盒）。origin = −diearea 左下角（P7：放置参考点溯源语义）。
int def_die_area_cbk(defrCallbackType_e, defiBox* box, defiUserData ud) {
    auto* ctx = static_cast<DefContext*>(ud);
    ++ctx->stats->die_area_count;

    const defiPoints pts = box->getPoint();
    const int32_t stack_dbu = ctx->stack->get_dbu_per_micron();
    CMVector<GEOPoint> poly_pts;
    int64_t xl = 0, yl = 0, xh = 0, yh = 0;
    for (int i = 0; i < pts.numPoints; ++i) {
        const int64_t x = pts.x[i];
        const int64_t y = pts.y[i];
        if (i == 0) {
            xl = xh = x;
            yl = yh = y;
        } else {
            xl = std::min(xl, x);
            yl = std::min(yl, y);
            xh = std::max(xh, x);
            yh = std::max(yh, y);
        }
        poly_pts.push_back(GEOPoint(
            static_cast<int32_t>(def_to_dbu(x, stack_dbu, ctx->def_units)),
            static_cast<int32_t>(def_to_dbu(y, stack_dbu, ctx->def_units))));
    }

    if (pts.numPoints > 2) {
        GEOPolygon polygon;
        polygon.get_ref_points() = std::move(poly_pts);
        ctx->current_cell.assign_polygon(std::move(polygon));
        ctx->current_cell.set_polygon();  // ㉞ is_polygon 位
    }

    const int32_t bx_low =
        static_cast<int32_t>(def_to_dbu(xl, stack_dbu, ctx->def_units));
    const int32_t by_low =
        static_cast<int32_t>(def_to_dbu(yl, stack_dbu, ctx->def_units));
    const int32_t bx_high =
        static_cast<int32_t>(def_to_dbu(xh, stack_dbu, ctx->def_units));
    const int32_t by_high =
        static_cast<int32_t>(def_to_dbu(yh, stack_dbu, ctx->def_units));
    ctx->current_cell.set_bbox(GEORect(bx_low, by_low, bx_high,
                                                by_high));
    // P7：block cell origin_ = −diearea 左下角（与 LEF ORIGIN 同位）
    ctx->current_cell.set_origin_x(static_cast<int32_t>(-bx_low));
    ctx->current_cell.set_origin_y(static_cast<int32_t>(-by_low));
    return 0;
}

int def_pin_cbk(defrCallbackType_e, defiPin* p, defiUserData ud) {
    auto* ctx = static_cast<DefContext*>(ud);
    if (!ctx->block_active) return 0;

    // 重名 port 保留首份（DSGN::0006 场景的数据源；R7 ㊱：DSPin 不存
    // name，查重经 port 名序列）
    const CMString name = p->pinName();
    for (const CMString& seen : ctx->port_names) {
        if (seen == name) {
            ++ctx->stats->skipped_port_count;
            return 0;
        }
    }

    // ㉙：port = DSPin（port 位 + placement_status_），不再是独立 DSPort；
    // R7 ㊱：pin 名不进 DSPin（登记 port_names 序列，汇总进 pin hasher）
    DSPin pin;
    pin.set_port();
    pin.set_type(map_port_use(p->hasUse() ? p->use() : "SIGNAL"));
    pin.set_direction(map_port_direction(p->hasDirection() ? p->direction()
                                                           : "INPUT"));

    // 逐层几何（block 局部坐标 → 全局 DBU）→ port 几何独立对象（R4：
    // 键 = 局部 pin 下标，汇总时按全局 pin id 重挂）。PORT 语法（5.7+）
    // 的几何与 placement 在 defiPinPort（PORT 块级，上游 P0 即此形态）；
    // 无 PORT 的旧式 pin 几何平铺在 defiPin::numLayer()。FIXED/COVER/
    // PLACED，UNPLACED 兜底记 PLACED（局部坐标）。
    bool placement_seen = false;
    const auto apply_placement = [&](bool fixed, bool cover) {
        pin.set_placement_status(
            fixed ? static_cast<uint8_t>(DSPinPlacementStatus::FIXED)
                  : cover ? static_cast<uint8_t>(DSPinPlacementStatus::COVER)
                          : static_cast<uint8_t>(DSPinPlacementStatus::PLACED));
        placement_seen = true;
    };
    CMVector<DSShapeRef> pin_geoms;
    const auto collect_rect = [&](const char* layer, int xl, int yl, int xh,
                                  int yh) {
        DSShapeRef ref;
        ref.layer_id_ = require_layer_id(layer, *ctx->stack);
        ref.set_rect(def_rect_to_dbu(xl, yl, xh, yh,
                                     ctx->stack->get_dbu_per_micron(),
                                     ctx->def_units));
        pin_geoms.push_back(std::move(ref));
    };

    if (p->numPorts() > 0) {
        // 放置状态取首个带 placement 的 PORT
        for (int i = 0; i < p->numPorts(); ++i) {
            const defiPinPort* pp = p->pinPort(i);
            if (!placement_seen && pp->hasPlacement()) {
                apply_placement(pp->isFixed() != 0, pp->isCover() != 0);
            }
            for (int k = 0; k < pp->numLayer(); ++k) {
                int xl = 0, yl = 0, xh = 0, yh = 0;
                pp->bounds(k, &xl, &yl, &xh, &yh);
                collect_rect(pp->layer(k), xl, yl, xh, yh);
            }
        }
    } else {
        for (int k = 0; k < p->numLayer(); ++k) {
            int xl = 0, yl = 0, xh = 0, yh = 0;
            p->bounds(k, &xl, &yl, &xh, &yh);
            collect_rect(p->layer(k), xl, yl, xh, yh);
        }
        if (p->hasPlacement()) {
            apply_placement(p->isFixed() != 0, p->isCover() != 0);
        }
    }
    if (!placement_seen) {
        pin.set_placement_status(
            static_cast<uint8_t>(DSPinPlacementStatus::PLACED));
    }

    const uint32_t local_id = ctx->current_cell.add_pin(std::move(pin));
    ctx->port_names.push_back(name);
    if (!pin_geoms.empty()) {
        ctx->port_geoms->add_geometries(local_id, std::move(pin_geoms));
    }
    ++ctx->stats->port_count;
    return 0;
}

// VIAS 段（S4b 通道）。矩形型：逐层 rect 按 stack 层型归属 cut/bottom/
// top；生成式（VIARULE 语句，5.6+ 语法固定全子句 CUTSIZE/LAYER 三层/
// CUTSPACING/ENCLOSURE）：按 DEF 自带参数展开（D13）——cut 为 CUTSIZE
// 中心对齐矩形，enclosure 为 cut 四边外扩。
int def_via_cbk(defrCallbackType_e, defiVia* v, defiUserData ud) {
    auto* ctx = static_cast<DefContext*>(ud);
    const int32_t stack_dbu = ctx->stack->get_dbu_per_micron();

    DSViaCell cell;
    cell.set_name(prefixed_via_name(ctx, v->name()));

    if (v->hasViaRule()) {
        // 生成式（D13 展开）
        char* rule_name = nullptr;
        int x_size = 0, y_size = 0;
        char* bot = nullptr;
        char* cut = nullptr;
        char* top = nullptr;
        int x_cs = 0, y_cs = 0, x_be = 0, y_be = 0, x_te = 0, y_te = 0;
        v->viaRule(&rule_name, &x_size, &y_size, &bot, &cut, &top, &x_cs,
                   &y_cs, &x_be, &y_be, &x_te, &y_te);

        cell.set_bottom_layer_id(require_layer_id(bot, *ctx->stack));
        cell.set_top_layer_id(require_layer_id(top, *ctx->stack));
        // ⑥ 通孔密度通道分层键：cut 层 id（DEF 参数权威）
        cell.set_cut_layer_id(require_layer_id(cut, *ctx->stack));

        const int64_t half_x = x_size / 2;
        const int64_t half_y = y_size / 2;
        const GEORect cut_rect = def_rect_to_dbu(
            -half_x, -half_y, x_size - half_x, y_size - half_y, stack_dbu,
            ctx->def_units);
        cell.add_cut_rect(cut_rect);

        // ENCLOSURE：cut 四边外扩（DEF 参数权威；collector 留作扩展）
        const auto expand = [&](int64_t enc_x, int64_t enc_y) {
            return def_rect_to_dbu(-half_x - enc_x, -half_y - enc_y,
                                   x_size - half_x + enc_x,
                                   y_size - half_y + enc_y, stack_dbu,
                                   ctx->def_units);
        };
        cell.add_bottom_enclosure(expand(x_be, y_be));
        cell.add_top_enclosure(expand(x_te, y_te));
        ++ctx->stats->viarule_via_count;
    } else {
        // 预定义（矩形型）
        uint32_t bottom = UINT32_MAX;
        uint32_t top = UINT32_MAX;
        uint32_t cut = UINT32_MAX;
        CMVector<uint32_t> layer_ids;
        for (int k = 0; k < v->numLayers(); ++k) {
            char* layer_name = nullptr;
            int xl = 0, yl = 0, xh = 0, yh = 0;
            v->layer(k, &layer_name, &xl, &yl, &xh, &yh);
            const uint32_t id = require_layer_id(layer_name, *ctx->stack);
            layer_ids.push_back(id);
            const bool is_cut =
                ctx->stack->layer_by_id(id).get_type() ==
                static_cast<uint8_t>(DSLayerType::CUT);
            if (is_cut) {
                cut = id;
            } else {
                if (bottom == UINT32_MAX || id < bottom) bottom = id;
                if (top == UINT32_MAX || id > top) top = id;
            }
        }
        cell.set_bottom_layer_id(bottom);
        cell.set_top_layer_id(top);
        cell.set_cut_layer_id(cut);
        for (int k = 0; k < v->numLayers(); ++k) {
            char* layer_name = nullptr;
            int xl = 0, yl = 0, xh = 0, yh = 0;
            v->layer(k, &layer_name, &xl, &yl, &xh, &yh);
            const GEORect rect = def_rect_to_dbu(
                xl, yl, xh, yh, stack_dbu, ctx->def_units);
            const bool is_cut =
                ctx->stack->layer_by_id(layer_ids[k]).get_type() ==
                static_cast<uint8_t>(DSLayerType::CUT);
            if (is_cut) {
                cell.add_cut_rect(rect);
            } else if (layer_ids[k] == bottom) {
                cell.add_bottom_enclosure(rect);
            } else {
                cell.add_top_enclosure(rect);
            }
        }
        ++ctx->stats->via_count;
    }

    add_via_unique(*ctx->vias, std::move(cell), *ctx->stats);
    return 0;
}

int def_design_end_cbk(defrCallbackType_e, void*, defiUserData ud) {
    auto* ctx = static_cast<DefContext*>(ud);
    if (ctx->block_active) {
        ctx->block_cells->push_back(std::move(ctx->current_cell));
        ++ctx->stats->block_count;
        ctx->current_cell = DSCell{};
        // port 名序列保留（R7 ㊱：与交付的 block cell pins_ 下标对齐，
        // 由 ds_parse_def_header 尾部统一 move 交出——每 DEF 至多 1 个
        // block cell，无跨 block 混装）
        ctx->block_active = false;
    }
    return 0;
}

}  // namespace

// ── S5a：COMPONENTS 责任链 ∥ 网名扫描（同一遍 DEF 读取）─────────────

namespace {

// S5a 回调上下文（经 defrRead userData 传递，无全局状态）
struct DefComponentsContext {
    const DSStack* stack;
    const DSDesign* design;
    const DSInstancePipeline* pipeline;
    DSBlockBuildData* block_data;
    DSDefComponentsStats* stats;
    // UNITS 缺省 100（防无 UNITS 语句文件除零，同 S4）
    int64_t def_units = 100;
    // 密度采样格边长（全局 DBU）；<= 0 = 不配置格网
    int32_t bin_dbu = 0;
    CMString design_name;
};

// defi placementStatus（DEFI_COMPONENT_*，1..5）→ DSPlacementStatus。
// SOFTFIXED（6.0 扩展）按 FIXED 近似（最小方案，语义最接近的合法放置
// 状态）；UNPLACED / 0（无 placement 子句）→ UNPLACED（D14 兜底计数）。
uint8_t map_component_status(int status) {
    switch (status) {
        case DEFI_COMPONENT_PLACED:
            return static_cast<uint8_t>(DSPlacementStatus::PLACED);
        case DEFI_COMPONENT_FIXED:
            return static_cast<uint8_t>(DSPlacementStatus::FIXED);
        case DEFI_COMPONENT_COVER:
            return static_cast<uint8_t>(DSPlacementStatus::COVER);
        case DEFI_COMPONENT_SOFTFIXED:
            return static_cast<uint8_t>(DSPlacementStatus::FIXED);
        default:
            return static_cast<uint8_t>(DSPlacementStatus::UNPLACED);
    }
}

int def_components_design_cbk(defrCallbackType_e, const char* name, defiUserData ud) {
    auto* ctx = static_cast<DefComponentsContext*>(ud);
    ctx->design_name = name;
    // ⑧ local 0 = block 自身占位（block cell id 经 cell hasher 查询，
    // 未命中 kInvalidId 仅占号；R7 ㊱ hash 化查询）
    const uint32_t block_cell_id =
        ctx->design->cell_names_.get_id(ctx->design_name);
    ctx->block_data->init_placeholder(
        ctx->design_name,
        DSCellNameHasher::is_valid_id(block_cell_id)
            ? block_cell_id
            : DSDesign::kInvalidId);
    return 0;
}

int def_components_units_cbk(defrCallbackType_e, double value, defiUserData ud) {
    auto* ctx = static_cast<DefComponentsContext*>(ud);
    ctx->def_units = static_cast<int64_t>(value);
    return 0;
}

// DIEAREA（S5a 仅用于密度格网配置）：T2 红线同 S4——getPoint() 取完整
// 点集聚合 bbox；格网原点 = diearea 左下角，行列数向上取整覆盖 diearea。
int def_components_die_area_cbk(defrCallbackType_e, defiBox* box, defiUserData ud) {
    auto* ctx = static_cast<DefComponentsContext*>(ud);
    if (ctx->bin_dbu <= 0) {
        return 0;
    }
    const defiPoints pts = box->getPoint();
    int64_t xl = 0, yl = 0, xh = 0, yh = 0;
    for (int i = 0; i < pts.numPoints; ++i) {
        if (i == 0) {
            xl = xh = pts.x[i];
            yl = yh = pts.y[i];
        } else {
            xl = std::min(xl, static_cast<int64_t>(pts.x[i]));
            yl = std::min(yl, static_cast<int64_t>(pts.y[i]));
            xh = std::max(xh, static_cast<int64_t>(pts.x[i]));
            yh = std::max(yh, static_cast<int64_t>(pts.y[i]));
        }
    }
    const int32_t stack_dbu = ctx->stack->get_dbu_per_micron();
    const int64_t ox = def_to_dbu(xl, stack_dbu, ctx->def_units);
    const int64_t oy = def_to_dbu(yl, stack_dbu, ctx->def_units);
    const int64_t width = def_to_dbu(xh, stack_dbu, ctx->def_units) - ox;
    const int64_t height = def_to_dbu(yh, stack_dbu, ctx->def_units) - oy;
    ctx->block_data->density_.configure(
        static_cast<int32_t>(ox), static_cast<int32_t>(oy), ctx->bin_dbu,
        ctx->bin_dbu,
        static_cast<uint32_t>((width + ctx->bin_dbu - 1) / ctx->bin_dbu),
        static_cast<uint32_t>((height + ctx->bin_dbu - 1) / ctx->bin_dbu));
    return 0;
}

int def_components_component_cbk(defrCallbackType_e, defiComponent* comp,
                          defiUserData ud) {
    auto* ctx = static_cast<DefComponentsContext*>(ud);
    DSInstanceContext ic;
    ic.instance_name = comp->id();
    ic.master_name = comp->name();
    ic.block_name = ctx->design_name;
    // 单位换算 int64 → int32（全局 DBU 域内，㉝ 统一基准）
    const int32_t stack_dbu = ctx->stack->get_dbu_per_micron();
    ic.placement = GEOPoint(
        static_cast<int32_t>(
            def_to_dbu(comp->placementX(), stack_dbu, ctx->def_units)),
        static_cast<int32_t>(
            def_to_dbu(comp->placementY(), stack_dbu, ctx->def_units)));
    // orient 整型直转（P5 零映射）；UNPLACED 的 defi 无效值（−1，语法
    // 红线：5.4+ UNPLACED 后随坐标被 parser 忽略）钳制为 N
    const int orient_raw = comp->placementOrient();
    ic.orient = (orient_raw >= 0 && orient_raw <= 7)
                    ? static_cast<GEOOrientation>(orient_raw)
                    : GEOOrientation::N;
    ic.placement_status = map_component_status(comp->placementStatus());
    ic.weight = comp->hasWeight() ? static_cast<double>(comp->weight()) : 0.0;
    ic.design = ctx->design;
    ic.block_data = ctx->block_data;

    ctx->pipeline->run(ic);
    ++ctx->stats->component_count;
    return 0;
}

// 网名扫描（③ NetNameOnly）：NETS 与 SPECIALNETS 共用 NetNameCbk（net
// 体经 SkipNetDetails/SkipSNetDetails 真跳过）。跨段重名保留首份 + 计数。
int def_components_net_name_cbk(defrCallbackType_e, const char* name,
                         defiUserData ud) {
    auto* ctx = static_cast<DefComponentsContext*>(ud);
    // R7 ㊱：重名判定经 net hasher 哨兵查询（原 contains 语义等价）
    if (DSNetNameHasher::is_valid_id(
            ctx->block_data->net_names_->get_id(name))) {
        ++ctx->stats->skipped_net_count;
        return 0;
    }
    ctx->block_data->register_net(name);
    ++ctx->stats->net_count;
    return 0;
}

}  // namespace

void ds_parse_def_components(const CMString& path, const DSStack& stack,
                             const DSDesign& design,
                             DSBlockBuildData& block_data,
                             DSDefComponentsStats& stats,
                             int32_t density_bin_dbu) {
    const DSInstancePipeline pipeline = ds_make_components_pipeline();

    DefComponentsContext ctx;
    ctx.stack = &stack;
    ctx.design = &design;
    ctx.pipeline = &pipeline;
    ctx.block_data = &block_data;
    ctx.stats = &stats;
    ctx.bin_dbu = density_bin_dbu;

    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) {
        throw std::runtime_error("ds_def_adapter: cannot open file '" + path +
                                 "'");
    }

    // 初始化序列（T2/T4 红线）：会话 → 默认回调补齐 → 注册（其后）→ 读
    defrInitSession();
    defrSetRegisterUnusedCallbacks();
    // NETS/SPECIALNETS 网体真跳过（网名照常回调）；COMPONENTS 不 skip
    //（③ NetNameOnly 场景不用 defrSetNetNameOnly——其连带 SkipComponents）
    defrSetSkipNetDetails(1);
    defrSetSkipSNetDetails(1);

    defrSetDesignCbk(def_components_design_cbk);
    defrSetUnitsCbk(def_components_units_cbk);
    defrSetDieAreaCbk(def_components_die_area_cbk);
    defrSetComponentCbk(def_components_component_cbk);
    defrSetNetNameCbk(def_components_net_name_cbk);

    int status;
    try {
        status = defrRead(f, path.c_str(), &ctx, 1);
    } catch (...) {
        // 回调内异常穿越读取器：清理文件句柄与会话后重抛
        std::fclose(f);
        defrClear();
        throw;
    }
    std::fclose(f);
    defrClear();
    if (status != 0) {
        throw std::runtime_error("ds_def_adapter: DEF parse failed with "
                                 "syntax/format error, file '" +
                                 path + "'");
    }
}

// ── S5b：网内容责任链 ∥ 分批多阶段（同一遍 DEF 读取）─────────────────

namespace {

// S5b 回调上下文（经 defrRead userData 传递，无全局状态）
struct DefNetsContext {
    const DSStack* stack;
    const DSDesign* design;
    const DSBlockBuildData* block_data;
    const DSNetPipeline* pipeline;
    DSNetBuildData* net_data;
    DSDefNetsStats* stats;
    // UNITS 缺省 100（防无 UNITS 语句文件除零，同 S4/S5a）
    int64_t def_units = 100;
    // 密度采样格边长（全局 DBU）；<= 0 = 不配置格网
    int32_t bin_dbu = 0;
    // 批界（alpha 键 net_batch_size；③ 分批落批）
    int net_batch_size = 1000;
    CMString design_name;
    // 批缓冲：按网收集的原解析上下文，达批界走链落批后释放
    CMVector<DSNetContext> batch;
};

// 批界冲刷：批内逐网走责任链，产物落批追加进 net_data，批缓冲释放。
// 空批（尾冲刷时批已清空）不产生批次计数。
void flush_nets_batch(DefNetsContext* ctx) {
    if (ctx->batch.empty()) {
        return;
    }
    for (DSNetContext& nctx : ctx->batch) {
        ctx->pipeline->run(nctx);
    }
    ctx->batch.clear();
    ++ctx->stats->batch_count;
}

// defiPath 单路径遍历 → wire 段 + via 引用（原解析数据）。
// 项序语义：VIA/VIADATA 的放置点为其后的 POINT 项（DEF 语法
// `VIA name ( x y )`）；VIAROTATION 不消费（⑩ via instance 仅需
// via cell id + 位置，orient 丢弃）；VIRTUALPOINT 不入几何；路径级
// RECT 补丁项忽略（net 级 RECT 语句另有通道，见 extract 主体）。
void traverse_nets_path(const defiPath* path, DefNetsContext* ctx,
                       CMVector<DSNetRawWire>& wires_out,
                       CMVector<DSNetRawVia>& vias_out) {
    const int32_t stack_dbu = ctx->stack->get_dbu_per_micron();
    const auto conv_x = [&](int64_t v) {
        return static_cast<int32_t>(def_to_dbu(v, stack_dbu, ctx->def_units));
    };

    DSNetRawWire cur;
    bool in_wire = false;
    DSNetRawVia pending;
    bool has_pending = false;
    const auto flush_wire = [&]() {
        if (in_wire && !cur.points.empty()) {
            wires_out.push_back(std::move(cur));
        }
        cur = DSNetRawWire{};
        in_wire = false;
    };
    const auto place_pending = [&](int x, int y) {
        pending.x = conv_x(x);
        pending.y = conv_x(y);
        vias_out.push_back(pending);
        has_pending = false;
        pending = DSNetRawVia{};
    };

    path->initTraverse();
    int item;
    while ((item = path->next()) != DEFIPATH_DONE) {
        switch (item) {
            case DEFIPATH_LAYER: {
                flush_wire();
                cur.layer_name = path->getLayer();
                in_wire = true;
                break;
            }
            case DEFIPATH_WIDTH:
                cur.width_dbu = conv_x(path->getWidth());
                break;
            case DEFIPATH_POINT: {
                int x = 0;
                int y = 0;
                path->getPoint(&x, &y);
                cur.points.emplace_back(conv_x(x), conv_x(y));
                if (has_pending) {
                    place_pending(x, y);
                }
                break;
            }
            case DEFIPATH_FLUSHPOINT: {
                int x = 0;
                int y = 0;
                int ext = 0;
                path->getFlushPoint(&x, &y, &ext);
                cur.points.emplace_back(conv_x(x), conv_x(y));
                if (has_pending) {
                    place_pending(x, y);
                }
                break;
            }
            case DEFIPATH_VIA:
                pending = DSNetRawVia{};
                pending.via_name = path->getVia();
                has_pending = true;
                break;
            case DEFIPATH_VIADATA: {
                int nx = 1;
                int ny = 1;
                int sx = 0;
                int sy = 0;
                path->getViaData(&nx, &ny, &sx, &sy);
                pending.num_x = nx;
                pending.num_y = ny;
                pending.step_x = conv_x(sx);
                pending.step_y = conv_x(sy);
                break;
            }
            default:
                break;  // VIAROTATION/VIRTUALPOINT/RECT 补丁/MASK 等忽略
        }
    }
    flush_wire();  // 路径收尾（挂起的 via 无后续放置点时丢弃）
}

// defiNet → 单网原解析上下文（连接项 + wire/rect/via 原数据，坐标换算
// 全局 DBU），入批并按批界冲刷
void extract_nets_net(defiNet* net, bool is_special, DefNetsContext* ctx) {
    DSNetContext nctx;
    nctx.net_name = net->name();
    nctx.is_special = is_special;
    nctx.stack = ctx->stack;
    nctx.design = ctx->design;
    nctx.block_data = ctx->block_data;
    nctx.net_data = ctx->net_data;
    nctx.design_name = ctx->design_name;

    // 连接项（instance pin / "PIN" port 引用，⑨ 拓扑保留原名）
    for (int i = 0; i < net->numConnections(); ++i) {
        DSNetRawConnection conn;
        conn.instance_name = net->instance(i);
        conn.pin_name = net->pin(i);
        nctx.connections.push_back(std::move(conn));
    }

    // wiring：defiWire → 逐 path 遍历（每个 ROUTED/NEW 语句一个 path）
    for (int w = 0; w < net->numWires(); ++w) {
        const defiWire* wire = net->wire(w);
        for (int p = 0; p < wire->numPaths(); ++p) {
            traverse_nets_path(wire->path(p), ctx, nctx.wires, nctx.vias);
        }
    }

    // net 级 RECT 项（5.6+，special/普通 net 同通道）
    const int32_t stack_dbu = ctx->stack->get_dbu_per_micron();
    for (int r = 0; r < net->numRectangles(); ++r) {
        DSNetRawRect rect;
        rect.layer_name = net->rectName(r);
        rect.rect = def_rect_to_dbu(net->xl(r), net->yl(r), net->xh(r),
                                    net->yh(r), stack_dbu, ctx->def_units);
        nctx.rects.push_back(std::move(rect));
    }

    // net 级 VIA 语句（5.8 special net `+ VIA name ( x y )`）
    for (int v = 0; v < net->numViaSpecs(); ++v) {
        DSNetRawVia via;
        via.via_name = net->viaName(v);
        const defiPoints pts = net->getViaPts(v);
        if (pts.numPoints > 0) {
            via.x = static_cast<int32_t>(
                def_to_dbu(pts.x[0], stack_dbu, ctx->def_units));
            via.y = static_cast<int32_t>(
                def_to_dbu(pts.y[0], stack_dbu, ctx->def_units));
        }
        nctx.vias.push_back(std::move(via));
    }

    ctx->batch.push_back(std::move(nctx));
    if (static_cast<int>(ctx->batch.size()) >= ctx->net_batch_size) {
        flush_nets_batch(ctx);
    }
}

int def_nets_design_cbk(defrCallbackType_e, const char* name, defiUserData ud) {
    auto* ctx = static_cast<DefNetsContext*>(ud);
    ctx->design_name = name;
    ctx->net_data->set_block_name(name);
    return 0;
}

int def_nets_units_cbk(defrCallbackType_e, double value, defiUserData ud) {
    auto* ctx = static_cast<DefNetsContext*>(ud);
    ctx->def_units = static_cast<int64_t>(value);
    return 0;
}

// DIEAREA：网侧密度格网配置（与 S5a 实例通道同参数、同口径）
int def_nets_die_area_cbk(defrCallbackType_e, defiBox* box, defiUserData ud) {
    auto* ctx = static_cast<DefNetsContext*>(ud);
    if (ctx->bin_dbu <= 0) {
        return 0;
    }
    const defiPoints pts = box->getPoint();
    int64_t xl = 0, yl = 0, xh = 0, yh = 0;
    for (int i = 0; i < pts.numPoints; ++i) {
        if (i == 0) {
            xl = xh = pts.x[i];
            yl = yh = pts.y[i];
        } else {
            xl = std::min(xl, static_cast<int64_t>(pts.x[i]));
            yl = std::min(yl, static_cast<int64_t>(pts.y[i]));
            xh = std::max(xh, static_cast<int64_t>(pts.x[i]));
            yh = std::max(yh, static_cast<int64_t>(pts.y[i]));
        }
    }
    const int32_t stack_dbu = ctx->stack->get_dbu_per_micron();
    const int64_t ox = def_to_dbu(xl, stack_dbu, ctx->def_units);
    const int64_t oy = def_to_dbu(yl, stack_dbu, ctx->def_units);
    const int64_t width = def_to_dbu(xh, stack_dbu, ctx->def_units) - ox;
    const int64_t height = def_to_dbu(yh, stack_dbu, ctx->def_units) - oy;
    ctx->net_data->density_.configure(
        static_cast<int32_t>(ox), static_cast<int32_t>(oy), ctx->bin_dbu,
        ctx->bin_dbu,
        static_cast<uint32_t>((width + ctx->bin_dbu - 1) / ctx->bin_dbu),
        static_cast<uint32_t>((height + ctx->bin_dbu - 1) / ctx->bin_dbu));
    return 0;
}

int def_nets_net_cbk(defrCallbackType_e, defiNet* net, defiUserData ud) {
    auto* ctx = static_cast<DefNetsContext*>(ud);
    extract_nets_net(net, /*is_special=*/false, ctx);
    return 0;
}

int def_nets_snet_cbk(defrCallbackType_e, defiNet* net, defiUserData ud) {
    auto* ctx = static_cast<DefNetsContext*>(ud);
    extract_nets_net(net, /*is_special=*/true, ctx);
    return 0;
}

}  // namespace

void ds_parse_def_nets(const CMString& path, const DSStack& stack,
                       const DSDesign& design,
                       const DSBlockBuildData& block_data,
                       DSNetBuildData& net_data, DSDefNetsStats& stats,
                       int32_t density_bin_dbu, int net_batch_size) {
    const DSNetPipeline pipeline = ds_make_nets_pipeline();

    DefNetsContext ctx;
    ctx.stack = &stack;
    ctx.design = &design;
    ctx.block_data = &block_data;
    ctx.pipeline = &pipeline;
    ctx.net_data = &net_data;
    ctx.stats = &stats;
    ctx.bin_dbu = density_bin_dbu;
    ctx.net_batch_size = net_batch_size > 0 ? net_batch_size : 1;

    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) {
        throw std::runtime_error("ds_def_adapter: cannot open file '" + path +
                                 "'");
    }

    // 初始化序列（T2/T4 红线）：会话 → 默认回调补齐 → 注册（其后）→ 读。
    // Skip 开关口径（S5b）：NETS/SPECIALNETS 细节不跳（defiNet 全量内容
    // 回调——网名 + 连接 + wire/via/rect 一遍收齐）；COMPONENTS 保持
    // parser 层真跳过（实例已由 S5a 处理）。SkipNetDetails 系字节级跳过
    // 与内容回调互斥，本遍置 0；各 parse 入口经 defrClear 全量重建会话，
    // S4/S5a 行为不受影响。
    defrInitSession();
    defrSetRegisterUnusedCallbacks();
    defrSetSkipComponents(1);

    defrSetDesignCbk(def_nets_design_cbk);
    defrSetUnitsCbk(def_nets_units_cbk);
    defrSetDieAreaCbk(def_nets_die_area_cbk);
    defrSetNetCbk(def_nets_net_cbk);
    defrSetSNetCbk(def_nets_snet_cbk);

    int status;
    try {
        status = defrRead(f, path.c_str(), &ctx, 1);
    } catch (...) {
        // 回调内异常穿越读取器：清理文件句柄与会话后重抛
        std::fclose(f);
        defrClear();
        throw;
    }
    std::fclose(f);
    defrClear();
    if (status != 0) {
        throw std::runtime_error("ds_def_adapter: DEF parse failed with "
                                 "syntax/format error, file '" +
                                 path + "'");
    }

    // 尾批冲刷 + 统计镜像（DSDefNetsStats = 产物内 DSNetStats + 批次数）
    flush_nets_batch(&ctx);
    stats.net_count = static_cast<int>(net_data.stats_.net_count);
    stats.connection_count = static_cast<int>(net_data.stats_.connection_count);
    stats.wire_count = static_cast<int>(net_data.stats_.wire_count);
    stats.rect_count = static_cast<int>(net_data.stats_.rect_count);
    stats.via_instance_count =
        static_cast<int>(net_data.stats_.via_instance_count);
    stats.skipped_via_count =
        static_cast<int>(net_data.stats_.skipped_via_count);
    stats.skipped_net_count =
        static_cast<int>(net_data.stats_.skipped_net_count);
}

// ── S4+S4b：DEF 头部一遍读取（原有入口）─────────────────────────────

void ds_parse_def_header(const CMString& path, const DSStack& stack,
                         CMVector<DSCell>& block_cells_out,
                         CMVector<CMString>& port_names_out,
                         DSPinGeometry& port_geoms_out,
                         CMVector<DSViaCell>& def_vias_out,
                         DSDefParseStats& stats) {
    DefContext ctx;
    ctx.stack = &stack;
    ctx.block_cells = &block_cells_out;
    ctx.port_geoms = &port_geoms_out;
    ctx.vias = &def_vias_out;
    ctx.stats = &stats;
    ctx.def_path = path;

    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) {
        throw std::runtime_error("ds_def_adapter: cannot open file '" + path +
                                 "'");
    }

    // 初始化序列（T2/T4 红线）：会话 → 默认回调补齐 → 注册（其后）→ 读
    defrInitSession();
    defrSetRegisterUnusedCallbacks();
    // 大段真跳过（接口探明见头注释）：S4 头扫描不为三大段构建对象
    defrSetSkipComponents(1);
    defrSetSkipNets(1);
    defrSetSkipSpecialNets(1);

    defrSetDesignCbk(def_design_cbk);
    defrSetDesignEndCbk(def_design_end_cbk);
    defrSetUnitsCbk(def_units_cbk);
    defrSetDieAreaCbk(def_die_area_cbk);
    defrSetPinCbk(def_pin_cbk);
    defrSetViaCbk(def_via_cbk);

    int status;
    try {
        status = defrRead(f, path.c_str(), &ctx, 1);
    } catch (...) {
        // 回调内异常（层引用缺失等）穿越读取器：清理文件句柄与会话后重抛
        std::fclose(f);
        defrClear();
        throw;
    }
    std::fclose(f);
    defrClear();
    if (status != 0) {
        throw std::runtime_error("ds_def_adapter: DEF parse failed with "
                                 "syntax/format error, file '" +
                                 path + "'");
    }
    // R7 ㊱：port pin 名随产物交出（与 block_cells_[0].pins_ 下标对齐；
    // 每 DEF 至多 1 个 block cell）
    port_names_out = std::move(ctx.port_names);
}

}  // namespace fly
