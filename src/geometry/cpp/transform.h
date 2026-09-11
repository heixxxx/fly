#pragma once

// =============================================================================
// GEO 放置变换 — geometry 独立模块（design-db-phase2-plan.md §3.1/§3.5）
//
// 放置语义（LEF/DEF Reference 5.8 + OpenDB 读入侧双源核对）：
//   - DEF COMPONENTS 的 placement 点 = 变换后放置包围盒的左下角；
//   - cell 几何不归一化、按原始坐标存储；instance 级存储 pos/orient
//     二元组：pos = cell 原坐标系 (0,0) 点的全局位置，换算只做一次
//     pos = t − apply_box(orient, box).ll()（box = 放置边界，
//     [−origin_, −origin_+(W,H)]；origin 修正只参与这一步）；
//   - instance 全局坐标 = R(orient)·m + pos（m = cell 原始坐标）；
//   - 层级复合（flatten）：compose(父, 子)，纯二元组复合零修正项。
//
// 成员与接口（模板真名 GEOTransformT<T>，业务别名 GEOTransform =
// GEOTransformT<int32_t>，命名规则见 DEVELOPMENT_GUIDELINES.md §2.2）：
//   GEOTransformT<T> {
//     GEOPointT<T>   offset_;   // 平移分量（业务语境 = pos）
//     GEOOrientation orient_;   // 旋转分量（D4 群元素，非模板）
//   };
//   apply(p)         = R(orient_)·p + offset_          // 点变换
//   apply_box(r)     = 四角点 apply 后 min/max 重排     // 矩形（归一化，
//                        W/E/FW/FE 下宽高换轴；x_high>=x_low 断言）
//   apply_polygon(p) = 逐顶点 apply、保持顶点序          // 多边形变换
//   compose(sub)     = 父∘子（先 sub 后本变换）          // 层级复合
//
// 纯旋转公式（§3.5.2 官方图示矢量级核对，OpenDB dbTransform::apply
// 同源；W = 逆时针 90°，FW = 先 FS 再逆时针 90°，FE = 先 FN 再逆时针 90°）：
//   N:(x,y)  W:(−y,x)  S:(−x,−y)  E:(y,−x)
//   FN:(−x,y) FS:(x,−y) FW:(y,x)  FE:(−y,−x)
//
// 模板约定：T ∈ {int32_t, int64_t, double}，与 GEOPointT/GEORectT/
// GEOPolygonT 实例化集合一致（头文件模板不强制显式实例化；业务使用
// 一律写无后缀别名 GEOTransform）。纯几何层，不抛业务异常；非法
// orient 为调用方契约错误（debug 断言）。
// =============================================================================

#include <geometry/cpp/geometry_types.h>

#include <common/serialization/cpp/serialization_macros.h>
#include <common/types/cpp/property_macro.h>

#include <algorithm>
#include <cassert>
#include <cstdint>

namespace fly {

// 8 方向枚举（D4 群），值严格对齐 Si2 defi 的 DEF_ORIENT_*（src/lefdef/
// def/include/defiNet.h）：N=0, W=1, S=2, E=3, FN=4, FW=5, FS=6, FE=7
// （FW 在 FS 之前——与 Si2 头文件一致）。defin 回调的 orient 整型直接
// static_cast<GEOOrientation>，零映射零转换。官方 Reference 表格的
// MX90/MY90（OA 系名）与 OpenDB 的 MXR90/MYR90 同值异名——跨源核对
// 锚定数学公式而非名字，OA 系名不进代码。
enum class GEOOrientation : uint8_t {
    N = 0,   // 恒等（R0）
    W = 1,   // 逆时针 90°（R90）
    S = 2,   // 180°（R180）
    E = 3,   // 逆时针 270°（R270）
    FN = 4,  // y 轴镜像（MY）
    FW = 5,  // 先 FS 再逆时针 90°（MX90，转置）
    FS = 6,  // x 轴镜像（MX）
    FE = 7,  // 先 FN 再逆时针 90°（MY90）
};

namespace geo_detail {

// 纯旋转（不含平移）：R(orient)·(x, y)。switch 展开八分支，编译器
// 可常量折叠；非法值 debug 断言（调用方契约错误）。
template<typename T>
inline GEOPointT<T> rotate(T x, T y, GEOOrientation o) {
    switch (o) {
        case GEOOrientation::N:
            return GEOPointT<T>(x, y);
        case GEOOrientation::W:
            return GEOPointT<T>(-y, x);
        case GEOOrientation::S:
            return GEOPointT<T>(-x, -y);
        case GEOOrientation::E:
            return GEOPointT<T>(y, -x);
        case GEOOrientation::FN:
            return GEOPointT<T>(-x, y);
        case GEOOrientation::FW:
            return GEOPointT<T>(y, x);
        case GEOOrientation::FS:
            return GEOPointT<T>(x, -y);
        case GEOOrientation::FE:
            return GEOPointT<T>(-y, -x);
    }
    assert(false && "非法 GEOOrientation 值（0-7 之外）");
    return GEOPointT<T>(x, y);
}

// D4 群 8×8 复合表（行 = 外层 orient a，列 = 内层 orient b；单元 =
// compose(a, b) 的 orient，即先 b 后 a）。数值与 OpenDB orientMul 复合
// 表同源（按 N..FE 值序重排）；正确性由单测 64 对全组合同态性质断言
// （compose(a,b).apply(p) == a.apply(b.apply(p))）与群锚定值锚定。
constexpr GEOOrientation k_orient_mul[8][8] = {
    // b=N W  S  E  FN FW FS FE      a
    {GEOOrientation::N, GEOOrientation::W, GEOOrientation::S,
     GEOOrientation::E, GEOOrientation::FN, GEOOrientation::FW,
     GEOOrientation::FS, GEOOrientation::FE},  // a=N
    {GEOOrientation::W, GEOOrientation::S, GEOOrientation::E,
     GEOOrientation::N, GEOOrientation::FE, GEOOrientation::FN,
     GEOOrientation::FW, GEOOrientation::FS},  // a=W
    {GEOOrientation::S, GEOOrientation::E, GEOOrientation::N,
     GEOOrientation::W, GEOOrientation::FS, GEOOrientation::FE,
     GEOOrientation::FN, GEOOrientation::FW},  // a=S
    {GEOOrientation::E, GEOOrientation::N, GEOOrientation::W,
     GEOOrientation::S, GEOOrientation::FW, GEOOrientation::FS,
     GEOOrientation::FE, GEOOrientation::FN},  // a=E
    {GEOOrientation::FN, GEOOrientation::FW, GEOOrientation::FS,
     GEOOrientation::FE, GEOOrientation::N, GEOOrientation::W,
     GEOOrientation::S, GEOOrientation::E},  // a=FN
    {GEOOrientation::FW, GEOOrientation::FS, GEOOrientation::FE,
     GEOOrientation::FN, GEOOrientation::E, GEOOrientation::N,
     GEOOrientation::W, GEOOrientation::S},  // a=FW
    {GEOOrientation::FS, GEOOrientation::FE, GEOOrientation::FN,
     GEOOrientation::FW, GEOOrientation::S, GEOOrientation::E,
     GEOOrientation::N, GEOOrientation::W},  // a=FS
    {GEOOrientation::FE, GEOOrientation::FN, GEOOrientation::FW,
     GEOOrientation::FS, GEOOrientation::W, GEOOrientation::S,
     GEOOrientation::E, GEOOrientation::N},  // a=FE
};

}  // namespace geo_detail

// 放置变换：平移 + 旋转二元组（point + rotation）。业务语境（design db
// instance）offset_ = pos（cell 原坐标系 (0,0) 点的全局位置）、orient_ =
// 放置朝向；纯几何类型，任何「平移后旋转、旋转后平移」场景均可复用。
template<typename T>
class GEOTransformT {
public:
    // 默认 = 恒等变换（零平移 + N）；offset_ 显式置零（GEOPointT 为
    // POD 风格不默认初始化，恒等语义须明确）
    GEOTransformT() : offset_(T{}, T{}), orient_(GEOOrientation::N) {}
    GEOTransformT(GEOPointT<T> offset, GEOOrientation orient)
        : offset_(offset), orient_(orient) {}
    GEOTransformT(T x, T y, GEOOrientation orient)
        : offset_(x, y), orient_(orient) {}

    // 平移分量
    GEOPointT<T> offset_;
    // 旋转分量
    GEOOrientation orient_ = GEOOrientation::N;

    CM_PROPERTY(offset)
    CM_PROPERTY(orient)

    // 点变换：R(orient_)·p + offset_（先旋转后平移）
    GEOPointT<T> apply(const GEOPointT<T>& p) const {
        const GEOPointT<T> r = geo_detail::rotate(p.get_x(), p.get_y(), orient_);
        return GEOPointT<T>(r.get_x() + offset_.get_x(),
                            r.get_y() + offset_.get_y());
    }

    // 矩形变换：四角点 apply 后 min/max 重排（归一化，x_high>=x_low）。
    // 前置条件：输入矩形满足 GEORectT 语义（debug 断言）；W/E/FW/FE 下
    // 宽高换轴。纯旋转场景用零偏移 transform 表达。
    GEORectT<T> apply_box(const GEORectT<T>& r) const {
        assert(r.get_x_high() >= r.get_x_low() && r.get_y_high() >= r.get_y_low());
        const GEOPointT<T> c1 = apply(GEOPointT<T>(r.get_x_low(), r.get_y_low()));
        const GEOPointT<T> c2 = apply(GEOPointT<T>(r.get_x_high(), r.get_y_low()));
        const GEOPointT<T> c3 = apply(GEOPointT<T>(r.get_x_high(), r.get_y_high()));
        const GEOPointT<T> c4 = apply(GEOPointT<T>(r.get_x_low(), r.get_y_high()));
        const T x_low = std::min({c1.get_x(), c2.get_x(), c3.get_x(), c4.get_x()});
        const T y_low = std::min({c1.get_y(), c2.get_y(), c3.get_y(), c4.get_y()});
        const T x_high = std::max({c1.get_x(), c2.get_x(), c3.get_x(), c4.get_x()});
        const T y_high = std::max({c1.get_y(), c2.get_y(), c3.get_y(), c4.get_y()});
        return GEORectT<T>(x_low, y_low, x_high, y_high);
    }

    // 多边形变换：逐顶点 apply、保持顶点序（顶点数与顺序不变）
    GEOPolygonT<T> apply_polygon(const GEOPolygonT<T>& poly) const {
        GEOPolygonT<T> out;
        out.get_ref_points().reserve(poly.get_points().size());
        for (const GEOPointT<T>& p : poly.get_points()) {
            out.get_ref_points().push_back(apply(p));
        }
        return out;
    }

    // 层级复合：父∘子（先应用 sub 再应用本变换）。
    //   offset = apply(sub.offset)、orient = 复合表 [orient_][sub.orient_]。
    // flatten 链式展开原语：compose(父, 子) 纯二元组复合，零修正项。
    GEOTransformT compose(const GEOTransformT& sub) const {
        return GEOTransformT(
            apply(sub.offset_),
            geo_detail::k_orient_mul[static_cast<uint8_t>(orient_)][
                static_cast<uint8_t>(sub.orient_)]);
    }

    FLY_SERIALIZE(offset_, orient_)
};

// 业务通用实例化别名（当前 int32，坐标范围裁定见 design-knowledge.md；
// 未来若需 64 位仅改此处实例化参数，使用点零改动）
using GEOTransform = GEOTransformT<int32_t>;

}  // namespace fly

using fly::GEOOrientation;
using fly::GEOTransform;
