// DEF 基线 driver：全计数回调直调 defrReader（回调空转只计数，模式同
// perf/defrw_perf_src.cpp 的全空回调）。start 回调记录段头声明值与次数，
// 对象回调记录逐条产出——两条独立解析路径供测试互相印证。
#include <lefdef/def/def/defrReader.hpp>

#include <algorithm>
#include <cstdio>

#include "baseline_drivers.hpp"

namespace def_baseline {

namespace {

DefCounts* g_counts = nullptr;

// 段头声明类回调（"SECTION N ;" 的 N 经 defrIntegerCbkFnType 下发）
int int_cbk(defrCallbackType_e type, int number, defiUserData) {
    DefCounts* c = g_counts;
    switch (type) {
        case defrViaStartCbkType:      ++c->via_start;      c->via_start_declared = number; break;
        case defrComponentStartCbkType: ++c->comp_start;    c->comp_start_declared = number; break;
        case defrStartPinsCbkType:     ++c->pins_start;     c->pins_start_declared = number; break;
        case defrNetStartCbkType:      ++c->net_start;      c->net_start_declared = number; break;
        case defrSNetStartCbkType:     ++c->snet_start;     c->snet_start_declared = number; break;
        case defrRegionStartCbkType:   ++c->region_start;   c->region_start_declared = number; break;
        case defrGroupsStartCbkType:   ++c->groups_start;   c->groups_start_declared = number; break;
        case defrSlotStartCbkType:     ++c->slot_start;     c->slot_start_declared = number; break;
        case defrFillStartCbkType:     ++c->fill_start;     c->fill_start_declared = number; break;
        case defrBlockageStartCbkType: ++c->blockage_start; c->blockage_start_declared = number; break;
        case defrNonDefaultStartCbkType: ++c->ndr_start;    c->ndr_start_declared = number; break;
        case defrStylesStartCbkType:   ++c->styles_start;   c->styles_start_declared = number; break;
        default: break;
    }
    return 0;
}

// 对象回调：每条解析产出的记录触发一次
int via_cbk(defrCallbackType_e, defiVia*, defiUserData) { ++g_counts->vias; return 0; }
int comp_cbk(defrCallbackType_e, defiComponent*, defiUserData) { ++g_counts->comps; return 0; }
int pin_cbk(defrCallbackType_e, defiPin*, defiUserData) { ++g_counts->pins; return 0; }
int net_cbk(defrCallbackType_e, defiNet*, defiUserData) { ++g_counts->nets; return 0; }
int snet_cbk(defrCallbackType_e, defiNet*, defiUserData) { ++g_counts->snets; return 0; }
int region_cbk(defrCallbackType_e, defiRegion*, defiUserData) { ++g_counts->regions; return 0; }
int group_cbk(defrCallbackType_e, defiGroup*, defiUserData) { ++g_counts->groups; return 0; }
int group_name_cbk(defrCallbackType_e, const char*, defiUserData) { ++g_counts->group_names; return 0; }
int slot_cbk(defrCallbackType_e, defiSlot*, defiUserData) { ++g_counts->slots; return 0; }
int fill_cbk(defrCallbackType_e, defiFill*, defiUserData) { ++g_counts->fills; return 0; }
int blockage_cbk(defrCallbackType_e, defiBlockage*, defiUserData) { ++g_counts->blockages; return 0; }
int ndr_cbk(defrCallbackType_e, defiNonDefault*, defiUserData) { ++g_counts->ndrs; return 0; }
int styles_cbk(defrCallbackType_e, defiStyles*, defiUserData) { ++g_counts->styles; return 0; }
int units_cbk(defrCallbackType_e, double number, defiUserData) { g_counts->units = number; return 0; }

// DIEAREA：上游 defiBox 的 xl/yl/xh/yh 是「前两点」的向后兼容赋值
// （见 defiSite.cpp addPoint 注释），非包围盒——经 getPoint() 取完整
// 点集后自行聚合 min/max。
int die_area_cbk(defrCallbackType_e, defiBox* box, defiUserData) {
    DefCounts* c = g_counts;
    ++c->die_area;
    defiPoints pts = box->getPoint();
    c->die_area_points = pts.numPoints;
    for (int i = 0; i < pts.numPoints; ++i) {
        if (i == 0) {
            c->die_xl = c->die_xh = pts.x[0];
            c->die_yl = c->die_yh = pts.y[0];
            continue;
        }
        c->die_xl = std::min(c->die_xl, pts.x[i]);
        c->die_yl = std::min(c->die_yl, pts.y[i]);
        c->die_xh = std::max(c->die_xh, pts.x[i]);
        c->die_yh = std::max(c->die_yh, pts.y[i]);
    }
    return 0;
}

}  // namespace

int parse_def(const char* path, DefCounts* counts) {
    *counts = DefCounts{};
    FILE* f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    g_counts = counts;

    // 初始化与上游工具 defrw/defrw_perf 序列对齐：defrInit 建会话 +
    // defrSetRegisterUnusedCallbacks 把未注册回调补成默认空处理
    // （lef 侧实测缺后者会在未注册回调路径段错误，def 侧同防）。
    defrInit();
    defrSetRegisterUnusedCallbacks();
    defrSetViaStartCbk(int_cbk);
    defrSetComponentStartCbk(int_cbk);
    defrSetStartPinsCbk(int_cbk);
    defrSetNetStartCbk(int_cbk);
    defrSetSNetStartCbk(int_cbk);
    defrSetRegionStartCbk(int_cbk);
    defrSetGroupsStartCbk(int_cbk);
    defrSetSlotStartCbk(int_cbk);
    defrSetFillStartCbk(int_cbk);
    defrSetBlockageStartCbk(int_cbk);
    defrSetNonDefaultStartCbk(int_cbk);
    defrSetStylesStartCbk(int_cbk);

    defrSetViaCbk(via_cbk);
    defrSetComponentCbk(comp_cbk);
    defrSetPinCbk(pin_cbk);
    defrSetNetCbk(net_cbk);
    defrSetSNetCbk(snet_cbk);
    defrSetRegionCbk(region_cbk);
    defrSetGroupCbk(group_cbk);
    defrSetGroupNameCbk(group_name_cbk);
    defrSetSlotCbk(slot_cbk);
    defrSetFillCbk(fill_cbk);
    defrSetBlockageCbk(blockage_cbk);
    defrSetNonDefaultCbk(ndr_cbk);
    defrSetStylesCbk(styles_cbk);
    defrSetUnitsCbk(units_cbk);
    defrSetDieAreaCbk(die_area_cbk);

    counts->status = defrRead(f, path, nullptr, 1);

    fclose(f);
    defrClear();
    g_counts = nullptr;
    return 0;
}

}  // namespace def_baseline
