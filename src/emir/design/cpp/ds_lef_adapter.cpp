#include <emir/design/cpp/ds_lef_adapter.h>

#include <lefdef/lef/lef/lefrReader.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <utility>

namespace fly {

namespace {

// —— DBU 换算：LEF 值（µm）× dbu_per_micron，四舍五入到最近整数
// （物理设计 DBU 网格对齐约定；面积 µm² → DBU²）——
int64_t to_dbu(double v, int32_t dbu) {
    return static_cast<int64_t>(
        std::llround(v * static_cast<double>(dbu)));
}

int64_t area_to_dbu(double v, int32_t dbu) {
    return static_cast<int64_t>(std::llround(
        v * static_cast<double>(dbu) * static_cast<double>(dbu)));
}

uint32_t require_layer_id(const char* name, const DSStack& stack) {
    const uint32_t id = stack.find_layer(name);
    if (id == DSStack::kNoLayer) {
        // 层引用未定义属格式错误（D15）——raise（调用方为解析回调，
        // 异常沿 bison C++ 栈展开传出 lefrRead）
        throw std::runtime_error(std::string("ds_lef_adapter: undefined "
                                             "layer reference '") +
                                 name + "'");
    }
    return id;
}

// 重名 via 保留首份（「抛弃并提醒」兜底；提醒消息 T6 接线）
const DSViaCell* find_via(const CMVector<DSViaCell>& vias,
                          const CMString& name) {
    for (const auto& v : vias) {
        if (v.get_name() == name) return &v;
    }
    return nullptr;
}

bool add_via_unique(CMVector<DSViaCell>& vias, DSViaCell&& cell,
                    DSLefParseStats& stats) {
    if (find_via(vias, cell.get_name()) != nullptr) {
        ++stats.via_conflict_count;
        return false;
    }
    vias.push_back(std::move(cell));
    ++stats.via_count;
    return true;
}

// LEF VIA → DSViaCell：逐层收集 rect；切割层（stack 类型 CUT）进
// cut_rects_，非切割层按 stack 序小者记 bottom、大者记 top（enclosure
// 即该层上的矩形）。层引用未定义 → raise（D15）。
DSViaCell convert_via(const lefiVia& v, const DSStack& stack, int32_t dbu) {
    struct LayerInfo {
        uint32_t id;
        bool is_cut;
    };
    CMVector<LayerInfo> infos;
    for (int k = 0; k < v.numLayers(); ++k) {
        const uint32_t id = require_layer_id(v.layerName(k), stack);
        infos.push_back({id, stack.layer_by_id(id).get_type() ==
                                 static_cast<uint8_t>(DSLayerType::CUT)});
    }

    uint32_t bottom = UINT32_MAX;
    uint32_t top = UINT32_MAX;
    uint32_t cut = UINT32_MAX;
    for (const auto& info : infos) {
        if (info.is_cut) {
            cut = info.id;  // ⑥ 通孔密度通道分层键
            continue;
        }
        if (bottom == UINT32_MAX || info.id < bottom) bottom = info.id;
        if (top == UINT32_MAX || info.id > top) top = info.id;
    }

    DSViaCell cell;
    cell.set_name(v.name());
    cell.set_bottom_layer_id(bottom);
    cell.set_top_layer_id(top);
    cell.set_cut_layer_id(cut);
    for (int k = 0; k < v.numLayers(); ++k) {
        const uint32_t id = infos[k].id;
        for (int r = 0; r < v.numRects(k); ++r) {
            const GEORect rect(
                to_dbu(v.xl(k, r), dbu), to_dbu(v.yl(k, r), dbu),
                to_dbu(v.xh(k, r), dbu), to_dbu(v.yh(k, r), dbu));
            if (infos[k].is_cut) {
                cell.add_cut_rect(rect);
            } else if (id == bottom) {
                cell.add_bottom_enclosure(rect);
            } else {
                cell.add_top_enclosure(rect);
            }
        }
    }
    return cell;
}

// LEF VIARULE GENERATE → 模板 DSViaCell 展开（㉚：DSViaRule 已删除，
// 规则按默认参数直接生成具体形状，中心对齐——cut 取规则 cut 层 RECT、
// enclosure 为 cut 四边外扩 overhang，同 DEF 生成式展开逻辑风格）。
// 层序约定：3 层 = 下布线 + cut（第 2 层）+ 上布线；非 GENERATE 规则
// （via 列表型）、层序非 3 层或 cut 层无 RECT 的规则无法展开，返回
// false（调用方跳过）。
bool expand_viarule(const lefiViaRule& vr, const DSStack& stack, int32_t dbu,
                    DSViaCell& out) {
    if (vr.hasGenerate() == 0 || vr.numLayers() != 3) {
        return false;
    }
    const lefiViaRuleLayer* bot = vr.layer(0);
    const lefiViaRuleLayer* cut = vr.layer(1);
    const lefiViaRuleLayer* top = vr.layer(2);
    if (!cut->hasRect()) {
        return false;
    }

    out = DSViaCell{};
    out.set_name(vr.name());
    out.set_bottom_layer_id(require_layer_id(bot->name(), stack));
    out.set_top_layer_id(require_layer_id(top->name(), stack));
    out.set_cut_layer_id(require_layer_id(cut->name(), stack));

    const int64_t cut_xl = to_dbu(cut->xl(), dbu);
    const int64_t cut_yl = to_dbu(cut->yl(), dbu);
    const int64_t cut_xh = to_dbu(cut->xh(), dbu);
    const int64_t cut_yh = to_dbu(cut->yh(), dbu);
    out.add_cut_rect(GEORect(
        static_cast<int32_t>(cut_xl), static_cast<int32_t>(cut_yl),
        static_cast<int32_t>(cut_xh), static_cast<int32_t>(cut_yh)));

    // enclosure：cut 四边外扩（overhang1 = x 方向、overhang2 = y 方向，
    // LEF「ENCLOSURE x y」语义；层缺 ENCLOSURE 时取 0 = 与 cut 同形）
    const auto expand = [&](const lefiViaRuleLayer* rl) {
        const int64_t ex =
            rl->hasEnclosure() ? to_dbu(rl->enclosureOverhang1(), dbu) : 0;
        const int64_t ey =
            rl->hasEnclosure() ? to_dbu(rl->enclosureOverhang2(), dbu) : 0;
        return GEORect(
            static_cast<int32_t>(cut_xl - ex), static_cast<int32_t>(cut_yl - ey),
            static_cast<int32_t>(cut_xh + ex), static_cast<int32_t>(cut_yh + ey));
    };
    out.add_bottom_enclosure(expand(bot));
    out.add_top_enclosure(expand(top));
    return true;
}

// LEF PIN 的 USE/DIRECTION 文本 → 枚举（uint8_t 存储）。宽松映射：
// USE 缺省 SIGNAL；DIRECTION 的 TRISTATE 记 OUTPUT、FEEDTHRU 记 INOUT。
uint8_t map_pin_use(const char* use) {
    if (std::strcmp(use, "POWER") == 0) {
        return static_cast<uint8_t>(DSPinType::POWER);
    }
    if (std::strcmp(use, "GROUND") == 0) {
        return static_cast<uint8_t>(DSPinType::GROUND);
    }
    return static_cast<uint8_t>(DSPinType::SIGNAL);
}

uint8_t map_pin_direction(const char* dir) {
    if (std::strcmp(dir, "OUTPUT") == 0 || std::strcmp(dir, "TRISTATE") == 0) {
        return static_cast<uint8_t>(DSPinDirection::OUTPUT);
    }
    if (std::strcmp(dir, "INOUT") == 0 || std::strcmp(dir, "FEEDTHRU") == 0) {
        return static_cast<uint8_t>(DSPinDirection::INOUT);
    }
    return static_cast<uint8_t>(DSPinDirection::INPUT);
}

// —— S1 tech lef 回调上下文（经 lefrRead userData 传递，无全局状态）——
struct TechContext {
    DSStack* stack;
    CMVector<DSViaCell>* vias;
    DSLefParseStats* stats;
    int32_t dbu = 0;  // UNITS 回调更新（LEF 语法 UNITS 先于 LAYER/VIA）
};

int tech_layer_cbk(lefrCallbackType_e, lefiLayer* l, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    const char* type = l->type();
    const bool is_routing = std::strcmp(type, "ROUTING") == 0;
    const bool is_cut = std::strcmp(type, "CUT") == 0;
    if (!is_routing && !is_cut) {
        // implant/masterslice/overlap 等：跳过并计数（D16）
        ++ctx->stats->skipped_layer_count;
        return 0;
    }

    DSLayer layer;
    layer.set_name(l->name());
    layer.set_type(static_cast<uint8_t>(is_cut ? DSLayerType::CUT
                                               : DSLayerType::ROUTING));
    if (l->hasDirection()) {
        const char* dir = l->direction();
        const uint8_t d = std::strcmp(dir, "HORIZONTAL") == 0
                              ? static_cast<uint8_t>(DSDirection::HORIZONTAL)
                              : std::strcmp(dir, "VERTICAL") == 0
                                    ? static_cast<uint8_t>(
                                          DSDirection::VERTICAL)
                                    : static_cast<uint8_t>(DSDirection::NONE);
        layer.set_direction(d);
    }
    if (l->hasWidth()) {
        layer.set_default_width(static_cast<int32_t>(
            to_dbu(l->width(), ctx->dbu)));
    }
    if (l->hasPitch()) {
        layer.set_pitch(static_cast<int32_t>(to_dbu(l->pitch(), ctx->dbu)));
    }
    for (int i = 0; i < l->numSpacing(); ++i) {
        layer.get_ref_spacing().push_back(
            static_cast<int32_t>(to_dbu(l->spacing(i), ctx->dbu)));
    }
    if (l->hasArea()) {
        layer.set_min_area(area_to_dbu(l->area(), ctx->dbu));
    }
    ctx->stack->add_layer(std::move(layer));
    ++ctx->stats->layer_count;
    return 0;
}

int tech_via_cbk(lefrCallbackType_e, lefiVia* v, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    add_via_unique(*ctx->vias, convert_via(*v, *ctx->stack, ctx->dbu),
                   *ctx->stats);
    return 0;
}

int tech_viarule_cbk(lefrCallbackType_e, lefiViaRule* vr, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    // ㉚：按规则默认参数直接展开模板 DSViaCell；无法展开的规则跳过
    // （非 GENERATE / 层序非 3 层 / cut 层无 RECT）
    DSViaCell expanded;
    if (expand_viarule(*vr, *ctx->stack, ctx->dbu, expanded)) {
        add_via_unique(*ctx->vias, std::move(expanded), *ctx->stats);
        ++ctx->stats->viarule_count;
    }
    return 0;
}

int tech_units_cbk(lefrCallbackType_e, lefiUnits* u, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    if (u->hasDatabase()) {
        ctx->dbu = static_cast<int32_t>(u->databaseNumber());
        ctx->stack->set_dbu_per_micron(ctx->dbu);
    }
    return 0;
}

int tech_manufacturing_cbk(lefrCallbackType_e, double value, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    // MANUFACTURINGGRID 单位为 µm（真实工艺可为 0.0005 这类小数）——
    // 经 DBU 换算取整（0.0005 µm @ DBU 2000 = 1 DBU）
    ctx->stack->set_manufacturing_grid(
        static_cast<int32_t>(to_dbu(value, ctx->dbu)));
    return 0;
}

// —— S2 cell lef 回调上下文 ——
// MACRO 生命周期：MacroBegin（建当前 cell，重名检查）→ PIN/OBS 逐条
// → MacroCbk（END MACRO，lefiMacro 完整属性回填 + add_cell 收录）。
struct CellContext {
    const DSStack* stack;
    DSDesign* design;
    DSPinGeometry* geometry;
    CMVector<DSViaCell>* vias;
    DSLefParseStats* stats;
    int32_t dbu = 0;  // stack 基准；UNITS 回调校验一致（D15）
    bool dbu_error = false;
    // R4：局部 pin id 平铺分配器（part 内跨 cell 单调——T6 汇总重挂按
    // 「pin_base + 局部 id」平移，要求局部 id 跨 cell 唯一）
    uint32_t next_pin_id = 0;

    // 当前 macro 构建状态（MacroBegin 置位，MacroCbk 收录/弃置）
    bool macro_valid = false;
    DSCell current_cell;
    CMVector<DSShapeRef> current_pin_geoms;
};

// lefiGeometries 遍历：LAYER 项切换当前层，RECT 项产出 DSShapeRef
// （pin 几何暂存 / obs 直入当前 cell）；polygon/path 等非矩形项跳过并
// 计数（兜底模式「跳过并计数」，发现遗漏可扩展）。
void collect_lef_geoms(const lefiGeometries* g, CellContext* ctx,
                       bool to_obs) {
    const char* cur_layer = "";
    for (int i = 0; i < g->numItems(); ++i) {
        switch (g->itemType(i)) {
            case lefiGeomLayerE:
                cur_layer = g->getLayer(i);
                break;
            case lefiGeomRectE: {
                const lefiGeomRect* r = g->getRect(i);
                DSShapeRef ref;
                ref.layer_id_ =
                    require_layer_id(cur_layer, *ctx->stack);
                ref.set_rect(GEORect(
                    static_cast<int32_t>(to_dbu(r->xl, ctx->dbu)),
                    static_cast<int32_t>(to_dbu(r->yl, ctx->dbu)),
                    static_cast<int32_t>(to_dbu(r->xh, ctx->dbu)),
                    static_cast<int32_t>(to_dbu(r->yh, ctx->dbu))));
                if (to_obs) {
                    ctx->current_cell.add_obs(ref);
                } else {
                    ctx->current_pin_geoms.push_back(std::move(ref));
                }
                break;
            }
            default:
                ++ctx->stats->skipped_geometry_count;
                break;
        }
    }
}

int cell_units_cbk(lefrCallbackType_e, lefiUnits* u, lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    if (u->hasDatabase()) {
        const int32_t v = static_cast<int32_t>(u->databaseNumber());
        if (v != ctx->dbu) {
            // lef 间 DBU 不一致（D15 格式错误）：标记后于解析完成统一
            // raise（异常不穿越 bison 栈，确保解析状态干净）
            ctx->dbu_error = true;
        }
    }
    return 0;
}

int cell_macro_begin_cbk(lefrCallbackType_e, const char* name,
                         lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    // 重名 macro 保留首份（裁定 ①/15 语义；后续 PIN/OBS 回调一并跳过）
    ctx->macro_valid = ctx->design->find_cell(name) == nullptr;
    if (!ctx->macro_valid) {
        ++ctx->stats->skipped_macro_count;
        return 0;
    }
    ctx->current_cell = DSCell{};
    ctx->current_cell.set_name(name);
    ctx->current_pin_geoms.clear();
    return 0;
}

int cell_pin_cbk(lefrCallbackType_e, lefiPin* p, lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    if (!ctx->macro_valid) return 0;

    DSPin pin;
    // R7 ㊱：pin 名不进 DSPin（仅全局/局部 pin hasher 的组合键）
    pin.set_type(map_pin_use(p->hasUse() ? p->use() : "SIGNAL"));
    pin.set_direction(map_pin_direction(p->hasDirection() ? p->direction()
                                                          : "INPUT"));
    // R4：局部 pin id 平铺分配（part 内跨 cell 单调）+ 局部 pin hasher
    // 注册；全局平铺 id 由 T6 汇总重排后回填 cell.pins_ 的 pin_id_
    // （ds_merge_cell_lef 经 hasher 反查组合键重挂的数据源）
    const CMString pin_name = p->name();
    ctx->current_cell.add_pin(std::move(pin));
    const uint32_t local_pin_id = ctx->next_pin_id++;
    ctx->design->register_pin(ctx->current_cell.get_name(), pin_name,
                              local_pin_id);
    // R4：pin id 分配即回填 DSPin::pin_id_（局部平铺 id；R7 ㊱ 汇总
    // ds_merge_cell_lef 按此 id 反查局部 hasher 组合键重挂——缺失则
    // 全部 pin 落在 id 0 造成重挂污染）
    ctx->current_cell.pins_.back().set_pin_id(local_pin_id);
    ++ctx->stats->pin_count;

    // ⑰：pin 几何不进简化 pin——收独立对象（R4：按局部平铺 pin id 逐
    // pin 落位；全局平铺 id 由 T6 汇总重排后按新 id 重挂）
    for (int i = 0; i < p->numPorts(); ++i) {
        collect_lef_geoms(p->port(i), ctx, /*to_obs=*/false);
    }
    if (!ctx->current_pin_geoms.empty()) {
        ctx->geometry->add_geometries(local_pin_id,
                                      std::move(ctx->current_pin_geoms));
        ctx->current_pin_geoms.clear();
    }
    return 0;
}

int cell_obstruction_cbk(lefrCallbackType_e, lefiObstruction* obs,
                         lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    if (!ctx->macro_valid) return 0;
    // D19：OBS 几何入库（cell 定义层）
    collect_lef_geoms(obs->geometries(), ctx, /*to_obs=*/true);
    return 0;
}

int cell_macro_cbk(lefrCallbackType_e, lefiMacro* m, lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    if (!ctx->macro_valid) return 0;

    // ㉗：cell lef 来源标记（lef_cell + macro_cell）
    ctx->current_cell.set_lef_cell();
    ctx->current_cell.set_macro_cell();
    if (m->hasClass()) ctx->current_cell.set_class(m->macroClass());
    if (m->hasSize()) {
        // ㉞：bbox 直接存放置包围盒（lef SIZE 矩形从 (0,0) 起）；尺寸由
        // bbox 派生，不再存 width_/height_ 字段
        ctx->current_cell.set_bbox(GEORect(
            0, 0, static_cast<int32_t>(to_dbu(m->sizeX(), ctx->dbu)),
            static_cast<int32_t>(to_dbu(m->sizeY(), ctx->dbu))));
    }
    if (m->hasOrigin()) {
        ctx->current_cell.set_origin_x(
            static_cast<int32_t>(to_dbu(m->originX(), ctx->dbu)));
        ctx->current_cell.set_origin_y(
            static_cast<int32_t>(to_dbu(m->originY(), ctx->dbu)));
    }
    if (m->hasSiteName()) ctx->current_cell.set_site(m->siteName());

    // 收录：add_cell 注册 namemap，局部 cell id = cells_ 下标（裁定 ①：
    // 全局 id 由 T6 汇总统一重排）。pin 几何已在 cell_pin_cbk 按局部
    // pin id 逐 pin 落位（R4），此处不再整 cell 收集
    ctx->design->add_cell(std::move(ctx->current_cell));
    ctx->current_cell = DSCell{};
    ++ctx->stats->macro_count;
    ctx->macro_valid = false;
    return 0;
}

int cell_via_cbk(lefrCallbackType_e, lefiVia* v, lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    add_via_unique(*ctx->vias, convert_via(*v, *ctx->stack, ctx->dbu),
                   *ctx->stats);
    return 0;
}

int cell_viarule_cbk(lefrCallbackType_e, lefiViaRule* vr, lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    // ㉚：cell lef 侧 VIARULE GENERATE 同样展开模板 DSViaCell（与 tech
    // lef 同一展开逻辑；展开产物进本文件 via 集，跨文件合并由 T6 汇总）
    DSViaCell expanded;
    if (expand_viarule(*vr, *ctx->stack, ctx->dbu, expanded)) {
        add_via_unique(*ctx->vias, std::move(expanded), *ctx->stats);
        ++ctx->stats->viarule_count;
    }
    return 0;
}

// 公共驱动：初始化序列（T2 红线）+ 回调注册 + 打开文件 + 读 + 清理。
// **回调注册必须发生在 lefrInitSession() 之后**——session 模式下先于
// init 的 Set 调用会被拒绝（实测报 "Attempt to call configuration
// function ... before lefrInit()"，T2 baseline 的 init→register→read
// 顺序即正确形态），故经 register_cbs 在会话建立后注册。
// lefrRead 非 0 = 语法/格式错误（raise）；调用方在返回后自行检查
// ctx 内标记的错误（如 DBU 不一致）并 raise（异常不穿越 bison 栈）。
void run_lefr(const CMString& path, void* context,
              const std::function<void()>& register_cbs) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) {
        throw std::runtime_error("ds_lef_adapter: cannot open file '" +
                                 path + "'");
    }
    lefrInitSession();
    lefrSetRegisterUnusedCallbacks();
    register_cbs();
    int status;
    try {
        status = lefrRead(f, path.c_str(), context);
    } catch (...) {
        // 回调内异常（层引用缺失等）穿越读取器：清理文件句柄与会话后重抛
        std::fclose(f);
        lefrClear();
        throw;
    }
    std::fclose(f);
    lefrClear();
    if (status != 0) {
        throw std::runtime_error("ds_lef_adapter: LEF parse failed with "
                                 "syntax/format error, file '" +
                                 path + "'");
    }
}

}  // namespace

DSLefParseStats ds_parse_tech_lef(const CMString& path, DSStack& stack,
                                  CMVector<DSViaCell>& tech_vias) {
    DSLefParseStats stats;
    TechContext ctx;
    ctx.stack = &stack;
    ctx.vias = &tech_vias;
    ctx.stats = &stats;
    ctx.dbu = stack.get_dbu_per_micron();  // UNITS 前的兜底（正常文件
                                           // UNITS 先于 LAYER/VIA）

    run_lefr(path, &ctx, [&] {
        lefrSetLayerCbk(tech_layer_cbk);
        lefrSetViaCbk(tech_via_cbk);
        lefrSetViaRuleCbk(tech_viarule_cbk);
        lefrSetUnitsCbk(tech_units_cbk);
        lefrSetManufacturingCbk(tech_manufacturing_cbk);
    });
    return stats;
}

DSLefParseStats ds_parse_cell_lef(const CMString& path, const DSStack& stack,
                                  DSDesign& design_out,
                                  DSPinGeometry& pin_geometry_out,
                                  CMVector<DSViaCell>& vias) {
    DSLefParseStats stats;
    CellContext ctx;
    ctx.stack = &stack;
    ctx.design = &design_out;
    ctx.geometry = &pin_geometry_out;
    ctx.vias = &vias;
    ctx.stats = &stats;
    ctx.dbu = stack.get_dbu_per_micron();

    run_lefr(path, &ctx, [&] {
        lefrSetUnitsCbk(cell_units_cbk);
        lefrSetMacroBeginCbk(cell_macro_begin_cbk);
        lefrSetPinCbk(cell_pin_cbk);
        lefrSetObstructionCbk(cell_obstruction_cbk);
        lefrSetMacroCbk(cell_macro_cbk);
        lefrSetViaCbk(cell_via_cbk);
        lefrSetViaRuleCbk(cell_viarule_cbk);
    });
    if (ctx.dbu_error) {
        throw std::runtime_error("ds_lef_adapter: DBU mismatch between cell "
                                 "lef and tech lef baseline (D15), file '" +
                                 path + "'");
    }
    return stats;
}

}  // namespace fly
