#pragma once

// =============================================================================
// GEO 几何通用结构 — geometry 独立模块
//
// design db（DS* 类型族）及后续 EDA 场景共用的基础几何类型，GEO 前缀
// 与模块归属对应（前缀 = 所属模块标识，见 DEVELOPMENT_GUIDELINES.md
// Section 2.2：geometry 独立模块 = GEO）。全部为头文件模板，实例化集合
// 约定 {int32_t, int64_t, double}：坐标 int32（DBU）、中间量/面积
// int64、浮点场景 double。
//
// 命名约定（DEVELOPMENT_GUIDELINES.md Section 2.2）：模板类以 T 后缀
// 命名（GEOPointT 等）；无后缀名 = 业务通用实例化别名（当前 int32，
// 坐标范围裁定见 docs/emir/design-knowledge.md）——位宽/类型变更只改
// 别名定义一处，使用点零改动。int64_t/double 为模板保留能力（仅单测
// 覆盖），无后缀别名之外不起其它别名。
//
// 类型清单（模板真名 → 业务别名）：
//   - GEOPointT<T>     二维点（x/y）                → GEOPoint
//   - GEORectT<T>      轴对齐矩形（左下 + 右上角点），配套半开区间归属
//                      判定（contains/overlaps：含下界不含上界，相邻图形
//                      不交叠——离散网格几何的标准语义）→ GEORect
//   - GEOPolygonT<T>   点集多边形（含包围盒 bbox）  → GEOPolygon
//
// 本模块为纯几何层，不含业务语义（带 layer id 的几何引用属 design
// 模块业务结构 DSShapeRef，见 src/emir/design/cpp/ds_types.h）。
//
// 属性访问经 CM_PROPERTY 宏生成五件套（get/get_ref/get_cref/set/
// set_move），成员命名遵循「属性名 + '_'」约束。经 FLY_SERIALIZE 支持
// 序列化（配合 FLY_EXPORT_SERIALIZE 绑定后可作为 fly db 对象持久化）。
// =============================================================================

#include <container/cpp/container_aliases.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <common/types/cpp/property_macro.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

namespace fly {

// 二维点。坐标类型 T ∈ {int32_t, int64_t, double}。
template<typename T>
class GEOPointT {
public:
    GEOPointT() = default;
    GEOPointT(T x, T y) : x_(x), y_(y) {}

    // x 坐标
    T x_;
    // y 坐标
    T y_;

    CM_PROPERTY(x)
    CM_PROPERTY(y)

    FLY_SERIALIZE(x_, y_)
};

// 轴对齐矩形，语义为 [x_low, x_high) × [y_low, y_high) 半开区间（判定
// 接口 contains/overlaps 与此配套）。前置条件 x_high >= x_low、
// y_high >= y_low（width/height 的 debug 断言）。
template<typename T>
class GEORectT {
public:
    GEORectT() = default;
    GEORectT(T x_low, T y_low, T x_high, T y_high)
        : x_low_(x_low), y_low_(y_low), x_high_(x_high), y_high_(y_high) {}

    // 左下角点 x
    T x_low_;
    // 左下角点 y
    T y_low_;
    // 右上角点 x
    T x_high_;
    // 右上角点 y
    T y_high_;

    CM_PROPERTY(x_low)
    CM_PROPERTY(y_low)
    CM_PROPERTY(x_high)
    CM_PROPERTY(y_high)

    // 宽度（x_high - x_low）。前置条件 x_high >= x_low，debug 断言。
    T width() const {
        assert(x_high_ >= x_low_);
        return x_high_ - x_low_;
    }

    // 高度（y_high - y_low）。前置条件 y_high >= y_low，debug 断言。
    T height() const {
        assert(y_high_ >= y_low_);
        return y_high_ - y_low_;
    }

    // 面积（整数实例化专用）：中间量与结果均走 int64，避免大坐标
    // 平方溢出。仅整数实例化可用——函数体内 static_assert 约束（成员
    // 函数体惰性实例化：浮点实例化本身合法，对其调用 area_int 即
    // 编译期报错；浮点面积走 area_double）。
    int64_t area_int() const {
        static_assert(std::is_integral_v<T>,
                      "area_int 仅整数实例化可用（浮点面积用 area_double）");
        return static_cast<int64_t>(width()) * static_cast<int64_t>(height());
    }

    // 面积（通用）：任意实例化可用，double 计算。
    double area_double() const {
        return static_cast<double>(width()) * static_cast<double>(height());
    }

    // 左下角点
    GEOPointT<T> low_left() const { return GEOPointT<T>(x_low_, y_low_); }

    // 右上角点
    GEOPointT<T> high_right() const { return GEOPointT<T>(x_high_, y_high_); }

    // 半开区间包含判定：x_low <= x < x_high && y_low <= y < y_high。
    // 点在左/下边界线上 = 包含；在右/上边界线上 = 不包含。
    bool contains(const GEOPointT<T>& p) const {
        return x_low_ <= p.get_x() && p.get_x() < x_high_ &&
               y_low_ <= p.get_y() && p.get_y() < y_high_;
    }

    // 半开区间交叠判定：任一方向区间投影均要求严格交叠（开区间比较），
    // 共享边界线的相邻矩形不算交叠。
    friend bool overlaps(const GEORectT& a, const GEORectT& b) {
        return a.x_low_ < b.x_high_ && b.x_low_ < a.x_high_ &&
               a.y_low_ < b.y_high_ && b.y_low_ < a.y_high_;
    }

    FLY_SERIALIZE(x_low_, y_low_, x_high_, y_high_)
};

// 点集多边形（顶点序即输入序；本层不解释绕向，布尔/裁剪等高级运算
// 由上层按需扩展）。
template<typename T>
class GEOPolygonT {
public:
    GEOPolygonT() = default;
    GEOPolygonT(std::initializer_list<GEOPointT<T>> pts) : points_(pts) {}

    // 顶点集
    CMVector<GEOPointT<T>> points_;

    CM_PROPERTY(points)

    // 包围盒。前置条件：点集非空（debug 断言）——空多边形无包围盒
    // 语义，调用方保证。
    GEORectT<T> bbox() const {
        assert(!points_.empty());
        T x_low = points_.front().get_x();
        T y_low = points_.front().get_y();
        T x_high = x_low;
        T y_high = y_low;
        for (const auto& p : points_) {
            x_low = std::min(x_low, p.get_x());
            y_low = std::min(y_low, p.get_y());
            x_high = std::max(x_high, p.get_x());
            y_high = std::max(y_high, p.get_y());
        }
        return GEORectT<T>(x_low, y_low, x_high, y_high);
    }

    FLY_SERIALIZE(points_)
};

// 业务通用实例化别名（当前 int32，坐标范围裁定见 design-knowledge.md；
// 未来若需 64 位仅改此处实例化参数，使用点零改动）
using GEOPoint = GEOPointT<int32_t>;
using GEORect = GEORectT<int32_t>;
using GEOPolygon = GEOPolygonT<int32_t>;

}  // namespace fly

using fly::GEOPoint;
using fly::GEOPolygon;
using fly::GEORect;
