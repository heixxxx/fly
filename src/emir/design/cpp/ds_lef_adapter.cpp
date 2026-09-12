#include <emir/design/cpp/ds_lef_adapter.h>

#include <lefdef/lef/lef/lefrReader.hpp>
#include <message/cpp/message_macros.h>  // MSG_FATAL_EXIT（DSGN::0017 tech lef 语法错误 fatal）

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <utility>

namespace fly {

namespace {

// —— DBU 换算：LEF 几何值为 µm 浮点（与文件 UNITS DATABASE MICRONS 声明
// 无关，声明值仅供工具一致性参考）——裁定 ㉝ 恒基准：恒乘全局基准
// kGlobalDbuPerMicron = 1000，四舍五入到最近整数（物理设计 DBU 网格对齐
// 约定；面积 µm² → DBU²）——
int64_t to_dbu(double v) {
    return static_cast<int64_t>(std::llround(
        v * static_cast<double>(DSStack::kGlobalDbuPerMicron)));
}

int64_t area_to_dbu(double v) {
    const double dbu = static_cast<double>(DSStack::kGlobalDbuPerMicron);
    return static_cast<int64_t>(std::llround(v * dbu * dbu));
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
// 即该层上的矩形）。层引用未定义 → 返回 false（条目级放弃该 via：
// DSGN::0010 提醒在 ds_resolve_layer_id 内，计数由调用方执行）。
bool convert_via(const lefiVia& v, const DSStack& stack, DSViaCell& out) {
    struct LayerInfo {
        uint32_t id;
        bool is_cut;
    };
    CMVector<LayerInfo> infos;
    for (int k = 0; k < v.numLayers(); ++k) {
        const uint32_t id = ds_resolve_layer_id(v.layerName(k), stack);
        if (id == DSStack::kNoLayer) {
            return false;
        }
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

    out = DSViaCell{};
    out.set_name(v.name());
    out.set_bottom_layer_id(bottom);
    out.set_top_layer_id(top);
    out.set_cut_layer_id(cut);
    for (int k = 0; k < v.numLayers(); ++k) {
        const uint32_t id = infos[k].id;
        for (int r = 0; r < v.numRects(k); ++r) {
            const GEORect rect(to_dbu(v.xl(k, r)), to_dbu(v.yl(k, r)),
                               to_dbu(v.xh(k, r)), to_dbu(v.yh(k, r)));
            if (infos[k].is_cut) {
                out.add_cut_rect(rect);
            } else if (id == bottom) {
                out.add_bottom_enclosure(rect);
            } else {
                out.add_top_enclosure(rect);
            }
        }
    }
    return true;
}

// LEF VIARULE GENERATE → 模板 DSViaCell 展开（㉚：DSViaRule 已删除，
// 规则按默认参数直接生成具体形状，中心对齐——cut 取规则 cut 层 RECT、
// enclosure 为 cut 四边外扩 overhang，同 DEF 生成式展开逻辑风格）。
// 层序约定：3 层 = 下布线 + cut（第 2 层）+ 上布线。
// 返回值语义（dev-rules §7 兜底分类，供调用方区分计数）：
//   false + *layer_ref_failed = true ：三层任一层引用未定义（DSGN::0010
//                                      提醒在 ds_resolve_layer_id 内，
//                                      调用方计 skipped_layer_ref_count）
//   false + *layer_ref_failed 不变 ：规则无法展开（非 GENERATE/层序非
//                                      3 层/cut 无 RECT，静默跳过）
// layer_ref_failed 可为 nullptr（不关心失败原因分类）。
bool expand_viarule(const lefiViaRule& vr, const DSStack& stack,
                    DSViaCell& out, bool* layer_ref_failed) {
    if (vr.hasGenerate() == 0 || vr.numLayers() != 3) {
        return false;
    }
    const lefiViaRuleLayer* bot = vr.layer(0);
    const lefiViaRuleLayer* cut = vr.layer(1);
    const lefiViaRuleLayer* top = vr.layer(2);
    if (!cut->hasRect()) {
        return false;
    }

    const uint32_t bot_id = ds_resolve_layer_id(bot->name(), stack);
    const uint32_t top_id = ds_resolve_layer_id(top->name(), stack);
    const uint32_t cut_id = ds_resolve_layer_id(cut->name(), stack);
    if (bot_id == DSStack::kNoLayer || top_id == DSStack::kNoLayer ||
        cut_id == DSStack::kNoLayer) {
        if (layer_ref_failed != nullptr) {
            *layer_ref_failed = true;
        }
        return false;
    }

    out = DSViaCell{};
    out.set_name(vr.name());
    out.set_bottom_layer_id(bot_id);
    out.set_top_layer_id(top_id);
    out.set_cut_layer_id(cut_id);

    const int64_t cut_xl = to_dbu(cut->xl());
    const int64_t cut_yl = to_dbu(cut->yl());
    const int64_t cut_xh = to_dbu(cut->xh());
    const int64_t cut_yh = to_dbu(cut->yh());
    out.add_cut_rect(GEORect(
        static_cast<int32_t>(cut_xl), static_cast<int32_t>(cut_yl),
        static_cast<int32_t>(cut_xh), static_cast<int32_t>(cut_yh)));

    // enclosure：cut 四边外扩（overhang1 = x 方向、overhang2 = y 方向，
    // LEF「ENCLOSURE x y」语义；层缺 ENCLOSURE 时取 0 = 与 cut 同形）
    const auto expand = [&](const lefiViaRuleLayer* rl) {
        const int64_t ex = rl->hasEnclosure() ? to_dbu(rl->enclosureOverhang1()) : 0;
        const int64_t ey = rl->hasEnclosure() ? to_dbu(rl->enclosureOverhang2()) : 0;
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
// UNITS 回调不注册（裁定 ㉝）：DATABASE MICRONS 声明不参与任何换算，
// 全部几何恒乘 kGlobalDbuPerMicron。
struct TechContext {
    DSStack* stack;
    CMVector<DSViaCell>* vias;
    DSLefParseStats* stats;
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
                                    : static_cast<uint8_t>(
                                          DSDirection::NONE);
        layer.set_direction(d);
    }
    if (l->hasWidth()) {
        layer.set_default_width(
            static_cast<int32_t>(to_dbu(l->width())));
    }
    if (l->hasPitch()) {
        layer.set_pitch(static_cast<int32_t>(to_dbu(l->pitch())));
    }
    for (int i = 0; i < l->numSpacing(); ++i) {
        layer.get_ref_spacing().push_back(
            static_cast<int32_t>(to_dbu(l->spacing(i))));
    }
    if (l->hasArea()) {
        layer.set_min_area(area_to_dbu(l->area()));
    }
    ctx->stack->add_layer(std::move(layer));
    ++ctx->stats->layer_count;
    return 0;
}

int tech_via_cbk(lefrCallbackType_e, lefiVia* v, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    // 层引用未定义 → 条目级放弃该 via（DSGN::0010 提醒已发）+ 计数
    DSViaCell cell;
    if (!convert_via(*v, *ctx->stack, cell)) {
        ++ctx->stats->skipped_layer_ref_count;
        return 0;
    }
    add_via_unique(*ctx->vias, std::move(cell), *ctx->stats);
    return 0;
}

int tech_viarule_cbk(lefrCallbackType_e, lefiViaRule* vr, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    // ㉚：按规则默认参数直接展开模板 DSViaCell；无法展开的规则跳过
    // （非 GENERATE / 层序非 3 层 / cut 层无 RECT，静默）；层引用未定义
    // 的规则同样放弃（条目级，计 skipped_layer_ref_count）
    DSViaCell expanded;
    bool layer_ref_failed = false;
    if (expand_viarule(*vr, *ctx->stack, expanded, &layer_ref_failed)) {
        add_via_unique(*ctx->vias, std::move(expanded), *ctx->stats);
        ++ctx->stats->viarule_count;
    } else if (layer_ref_failed) {
        ++ctx->stats->skipped_layer_ref_count;
    }
    return 0;
}

int tech_manufacturing_cbk(lefrCallbackType_e, double value, lefiUserData ud) {
    auto* ctx = static_cast<TechContext*>(ud);
    // MANUFACTURINGGRID 单位为 µm（真实工艺可为 0.0005 这类小数）——
    // 经恒基准换算取整（0.0005 µm × 1000 DBU/µm = 0.5 → 四舍五入 1 DBU）
    ctx->stack->set_manufacturing_grid(
        static_cast<int32_t>(to_dbu(value)));
    return 0;
}

// —— S2 cell lef 回调上下文 ——
// MACRO 生命周期：MacroBegin（建当前 cell，重名检查）→ PIN/OBS 逐条
// → MacroCbk（END MACRO，lefiMacro 完整属性回填 + add_cell 收录）。
// UNITS 回调不注册（裁定 ㉝）：DATABASE MICRONS 声明不做一致性校验、
// 不参与换算（lef 间 DBU 声明不一致不再 raise，D15 该子项撤销）。
struct CellContext {
    const DSStack* stack;
    DSDesign* design;
    DSPinGeometry* geometry;
    CMVector<DSViaCell>* vias;
    DSLefParseStats* stats;
    // R4：局部 pin id 平铺分配器（part 内跨 cell 单调——T6 汇总重挂按
    // 「pin_base + 局部 id」平移，要求局部 id 跨 cell 唯一）
    uint32_t next_pin_id = 0;

    // 当前 macro 构建状态（MacroBegin 置位，MacroCbk 收录/弃置）
    bool macro_valid = false;
    DSCell current_cell;
    CMVector<DSShapeRef> current_pin_geoms;
};

// lefiGeometries 遍历：LAYER 项切换当前层，RECT 项产出 DSShapeRef
// （pin 几何暂存 / obs 直入当前 cell）；层引用未定义的 rect 条目级丢弃
// + 计数（DSGN::0010）；polygon/path 等非矩形项跳过并计数（兜底模式
// 「跳过并计数」，发现遗漏可扩展）。
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
                const uint32_t layer_id =
                    ds_resolve_layer_id(cur_layer, *ctx->stack);
                if (layer_id == DSStack::kNoLayer) {
                    ++ctx->stats->skipped_layer_ref_count;
                    break;
                }
                DSShapeRef ref;
                ref.layer_id_ = layer_id;
                ref.set_rect(GEORect(
                    static_cast<int32_t>(to_dbu(r->xl)),
                    static_cast<int32_t>(to_dbu(r->yl)),
                    static_cast<int32_t>(to_dbu(r->xh)),
                    static_cast<int32_t>(to_dbu(r->yh))));
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
            0, 0, static_cast<int32_t>(to_dbu(m->sizeX())),
            static_cast<int32_t>(to_dbu(m->sizeY()))));
    }
    if (m->hasOrigin()) {
        ctx->current_cell.set_origin_x(
            static_cast<int32_t>(to_dbu(m->originX())));
        ctx->current_cell.set_origin_y(
            static_cast<int32_t>(to_dbu(m->originY())));
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
    // 层引用未定义 → 条目级放弃该 via（DSGN::0010 提醒已发）+ 计数
    DSViaCell cell;
    if (!convert_via(*v, *ctx->stack, cell)) {
        ++ctx->stats->skipped_layer_ref_count;
        return 0;
    }
    add_via_unique(*ctx->vias, std::move(cell), *ctx->stats);
    return 0;
}

int cell_viarule_cbk(lefrCallbackType_e, lefiViaRule* vr, lefiUserData ud) {
    auto* ctx = static_cast<CellContext*>(ud);
    // ㉚：cell lef 侧 VIARULE GENERATE 同样展开模板 DSViaCell（与 tech
    // lef 同一展开逻辑；展开产物进本文件 via 集，跨文件合并由 T6 汇总）；
    // 层引用未定义的规则条目级放弃 + 计数（与 tech 侧同口径）
    DSViaCell expanded;
    bool layer_ref_failed = false;
    if (expand_viarule(*vr, *ctx->stack, expanded, &layer_ref_failed)) {
        add_via_unique(*ctx->vias, std::move(expanded), *ctx->stats);
        ++ctx->stats->viarule_count;
    } else if (layer_ref_failed) {
        ++ctx->stats->skipped_layer_ref_count;
    }
    return 0;
}

// 公共驱动：初始化序列（T2 红线）+ 回调注册 + 打开文件 + 读 + 清理。
// **回调注册必须发生在 lefrInitSession() 之后**——session 模式下先于
// init 的 Set 调用会被拒绝（实测报 "Attempt to call configuration
// function ... before lefrInit()"，T2 baseline 的 init→register→read
// 顺序即正确形态），故经 register_cbs 在会话建立后注册。
// 文件不可读仍 raise（dev-rules §7 第一类）；回调内异常穿越读取器清理
// 后重抛（编程错误守卫不转 fatal）。语法/格式错误不在此 raise——经
// syntax_failed 出参交调用方按范式处置（2026-09-13：tech lef fatal
// DSGN::0017 / cell lef 兜底空产物，见 dev-rules §7.2）。
void run_lefr(const CMString& path, void* context,
              const std::function<void()>& register_cbs,
              bool* syntax_failed) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (f == nullptr) {
        throw std::runtime_error("ds_lef_adapter: cannot open file '" +
                                 path + "'");
    }
    *syntax_failed = false;
    lefrInitSession();
    lefrSetRegisterUnusedCallbacks();
    register_cbs();
    int status;
    try {
        status = lefrRead(f, path.c_str(), context);
    } catch (...) {
        // 回调内异常穿越读取器：清理文件句柄与会话后重抛
        std::fclose(f);
        lefrClear();
        throw;
    }
    std::fclose(f);
    lefrClear();
    if (status != 0) {
        *syntax_failed = true;
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

    bool syntax_failed = false;
    run_lefr(path, &ctx, [&] {
        lefrSetLayerCbk(tech_layer_cbk);
        lefrSetViaCbk(tech_via_cbk);
        lefrSetViaRuleCbk(tech_viarule_cbk);
        lefrSetManufacturingCbk(tech_manufacturing_cbk);
        // UNITS 不注册（裁定 ㉝）：DATABASE MICRONS 声明不写 stack、不
        // 参与换算——dbu_per_micron_ 恒 kGlobalDbuPerMicron = 1000
    }, &syntax_failed);
    if (syntax_failed) {
        // 范式 (a)（2026-09-13 裁定）：层表来源损坏无法兜底——下游一切
        // 层引用/几何换算都失真，fatal 结束整个 run（码 80 + master 联动）。
        MSG_FATAL_EXIT("DSGN::0017", 0, 80,
                       "tech lef parse failed with syntax/format error, "
                       "file '{}'", path);
    }
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

    bool syntax_failed = false;
    run_lefr(path, &ctx, [&] {
        lefrSetMacroBeginCbk(cell_macro_begin_cbk);
        lefrSetPinCbk(cell_pin_cbk);
        lefrSetObstructionCbk(cell_obstruction_cbk);
        lefrSetMacroCbk(cell_macro_cbk);
        lefrSetViaCbk(cell_via_cbk);
        lefrSetViaRuleCbk(cell_viarule_cbk);
        // UNITS 不注册（裁定 ㉝）：cell lef 声明与 tech lef 不一致不再
        // 校验（dbu_error 标记与尾部 raise 已删除）
    }, &syntax_failed);
    if (syntax_failed) {
        // 范式 (b)（2026-09-13 裁定）：单 cell lef 语法错误兜底——语法错
        // 误后回调产物不可信，清空本文件全部产物（空 DSDesign + pin 几何
        // 空 + via 空）交汇总照常合并（cell 缺失由 fake cell 承接引用，
        // DSGN::0007）；失败标记随 stats 出参上交，DSGN::0014 由 flow 汇总
        // 层发（部分失败）/ DSGN::0015 fatal（全部失败）。任务不 FAILED、
        // 依赖链保持满足。
        design_out = DSDesign();
        pin_geometry_out = DSPinGeometry();
        vias.clear();
        stats = DSLefParseStats();
        stats.parse_failed_count = 1;
    }
    return stats;
}

}  // namespace fly
