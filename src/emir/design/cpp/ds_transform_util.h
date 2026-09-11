#pragma once

// =============================================================================
// R6 transform 业务接入的 design 侧薄适配（design-db-phase2-plan.md §1 R6 /
// §3.1）：defin 回调 orient 整型直转（与 GEOOrientation 值同源零映射，P5）
// + DEF placement t → instance 存储值 pos 的放置边界换算（origin_/box 用
// 一次，㉜ 最终形态）。
//
// 语义（LEF/DEF Reference 5.8 + OpenDB 读入侧双源核对，方案 §3.5）：
//   - DEF COMPONENTS 的 placement 点 t = 变换后放置包围盒的左下角；
//   - 放置边界 box = [−origin_, −origin_+(W,H)]（§3.1 权威公式，对 LEF
//     cell 与 block cell 同构：LEF cell origin_ = ORIGIN 值 → box = SIZE
//     框平移 −ORIGIN；block cell origin_ = −diearea_ll → box 恰为
//     diearea 矩形，P7）；
//   - pos = t − R(orient)·box 的左下角（cell 原始坐标系 (0,0) 点的全局
//     位置）；instance 全局坐标 = R(orient)·m + pos（m = cell 原始坐标）；
//   - origin 修正只参与本换算一次，instance 级 transform（pos/orient
//     二元组）与后续几何应用完全不含 origin_。
//
// 使用示例（§3.3 例 1，DBU@1000）：
//   DSCell inv;  // SIZE 0.8×0.4、ORIGIN (0.1,0)：bbox (0,0,800,400)、
//                // origin (100,0)；pin Z 原值 (600,100)-(700,300)
//   GEOTransform inst = place_from_def({100000, 200000}, 1, inv);
//   //   → pos (100400, 200100) + W；pin 全局 = inst.apply_box(pin 原值)
// 层级复合（flatten，§3.1 变换链）：子全局 = 父.compose(子)，纯二元组
// 复合零修正项——直接用 geometry 的 GEOTransform::compose。
// =============================================================================

#include <emir/design/cpp/ds_types.h>
#include <geometry/cpp/transform.h>

namespace fly {

// t → pos 换算的几何核心（bbox/origin 直取版本）：供责任链在 cell 数据
// 不经 DSCell 承载时复用（fake cell 占位 1×1 的等价输入）。
inline GEOTransform place_from_def(GEOPoint t, int orient_int,
                                            const GEORect& bbox,
                                            int32_t origin_x, int32_t origin_y) {
    // defin 回调 orient 整型与 GEOOrientation 值同源（N=0..FE=7，
    // Si2 DEF_ORIENT_*），直转零映射（P5 裁定）
    const GEOOrientation o = static_cast<GEOOrientation>(orient_int);
    // 放置边界 box = [−origin_, −origin_+(W,H)]（宽高由 bbox 派生）
    const GEORect box(-origin_x, -origin_y,
                               -origin_x + bbox.width(),
                               -origin_y + bbox.height());
    // pos = t − R(orient)·box 的左下角（纯旋转零偏移变换）
    const GEORect rotated =
        GEOTransform(GEOPoint(0, 0), o).apply_box(box);
    const GEOPoint pos(t.get_x() - rotated.get_x_low(),
                                t.get_y() - rotated.get_y_low());
    return GEOTransform(pos, o);
}

// DEF placement → instance transform（R6 业务接入主入口）
inline GEOTransform place_from_def(GEOPoint t, int orient_int,
                                            const DSCell& cell) {
    return place_from_def(t, orient_int, cell.get_bbox(), cell.get_origin_x(),
                          cell.get_origin_y());
}

}  // namespace fly
