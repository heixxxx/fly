// LEF 基线 driver：全计数回调直调 lefrReader（回调空转只计数，
// 模式同 perf/defrw_perf_src.cpp 的全空回调）。独立翻译单元——
// 上游 lef/lef/lex.h 实体不外泄到其他 TU。
#include <lefdef/lef/lef/lefrReader.hpp>

#include <cstdio>

#include "baseline_drivers.hpp"

namespace lef_baseline {

namespace {

LefCounts* g_counts = nullptr;

int macro_cbk(lefrCallbackType_e, lefiMacro*, lefiUserData) {
    ++g_counts->macros;
    return 0;
}

int layer_cbk(lefrCallbackType_e, lefiLayer*, lefiUserData) {
    ++g_counts->layers;
    return 0;
}

int via_cbk(lefrCallbackType_e, lefiVia*, lefiUserData) {
    ++g_counts->vias;
    return 0;
}

int pin_cbk(lefrCallbackType_e, lefiPin*, lefiUserData) {
    ++g_counts->pins;
    return 0;
}

}  // namespace

int parse_lef(const char* path, LefCounts* counts) {
    *counts = LefCounts{};
    FILE* f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    g_counts = counts;

    // 初始化三件套（与上游工具 lefrw 的序列对齐，缺一不可）：
    //   - lefrInitSession：6.0 解析数据挂会话，lefrInit 旧入口不建会话
    //     会空指针行为（VIA 段后段错误，已实测）；
    //   - lefrSetRegisterUnusedCallbacks：把未注册的回调补成默认空处理，
    //     否则个别内部路径直接走空指针（同上实测）。
    lefrInitSession();
    lefrSetRegisterUnusedCallbacks();
    lefrSetMacroCbk(macro_cbk);
    lefrSetLayerCbk(layer_cbk);
    lefrSetViaCbk(via_cbk);
    lefrSetPinCbk(pin_cbk);

    counts->status = lefrRead(f, path, nullptr);

    fclose(f);
    lefrClear();
    g_counts = nullptr;
    return 0;
}

}  // namespace lef_baseline
