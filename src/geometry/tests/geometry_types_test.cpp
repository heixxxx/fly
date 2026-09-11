// GEO 几何通用结构单测：三类型（GEOPoint/GEORect/GEOPolygon）的
// CM_PROPERTY 生成接口、GEORect 半开区间 contains/overlaps 边界用例、
// 面积（整数 int64 中间量防溢出 / 浮点）、bbox、以及三类型 ×
// {int32_t, int64_t, double} 实例化集合的 FLY_SERIALIZE 序列化往返。
// （带 layer id 的几何引用属 design 业务结构 DSShapeRef，其用例在
// src/emir/design/tests/ds_types_test.cpp。）
#include <geometry/cpp/geometry_types.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace {

using namespace fly;

// —— GEOPoint：构造 + CM_PROPERTY 五接口 ——
TEST(GEOPointTest, ConstructAndPropertyAccessors) {
    GEOPoint p(3, 5);
    EXPECT_EQ(p.get_x(), 3);
    EXPECT_EQ(p.get_y(), 5);

    // set 拷贝 / set_move
    GEOPointT<CMString> ps;
    CMString name = "pin_a";
    ps.set_x_move(std::move(name));
    ps.set_y("net_b");
    EXPECT_TRUE(name.empty());
    EXPECT_EQ(ps.get_x(), "pin_a");
    EXPECT_EQ(ps.get_y(), "net_b");

    // get_ref 原地修改 / get_cref 只读
    ps.get_ref_x() += "_ext";
    const GEOPointT<CMString>& cps = ps;
    EXPECT_EQ(cps.get_cref_x(), "pin_a_ext");
    EXPECT_EQ(cps.get_y(), "net_b");
}

// —— GEORect：width/height/角点 ——
TEST(GEORectTest, WidthHeightAndCorners) {
    GEORect r(10, 20, 50, 90);
    EXPECT_EQ(r.width(), 40);
    EXPECT_EQ(r.height(), 70);
    EXPECT_EQ(r.area_double(), 2800.0);

    EXPECT_EQ(r.low_left().get_x(), 10);
    EXPECT_EQ(r.low_left().get_y(), 20);
    EXPECT_EQ(r.high_right().get_x(), 50);
    EXPECT_EQ(r.high_right().get_y(), 90);
}

// —— GEORect：面积（整数 int64 中间量防溢出 / 浮点）——
TEST(GEORectTest, AreaIntegralUsesInt64Intermediate) {
    // 65536² = 4294967296 > int32 上限：验证 int64 中间量不溢出
    GEORect r(0, 0, 65536, 65536);
    EXPECT_EQ(r.area_int(), 4294967296LL);

    GEORectT<int64_t> big(-1000000, -2000000, 3000000, 4000000);
    EXPECT_EQ(big.area_int(), 4000000LL * 6000000LL);

    // 「仅整数实例化」约束由函数体内 static_assert 实现（编译期报错，
    // 无法在运行时测试中验证负向路径，浮点实例上不调用 area_int 即可）。
}

TEST(GEORectTest, AreaDoubleGeneric) {
    // 通用面积对浮点/整数实例化均可用于
    GEORectT<double> rf(0.5, 0.25, 2.5, 4.25);
    EXPECT_DOUBLE_EQ(rf.area_double(), 2.0 * 4.0);

    GEORect ri(1, 2, 4, 6);
    EXPECT_DOUBLE_EQ(ri.area_double(), 12.0);
}

// —— GEORect：半开区间 contains 边界用例 ——
TEST(GEORectTest, ContainsHalfOpenBoundaries) {
    GEORect r(10, 20, 50, 90);

    // 左/下边界线上的点 = 包含（low-left 角点恰在边界线上）
    EXPECT_TRUE(r.contains(GEOPoint(10, 20)));
    EXPECT_TRUE(r.contains(GEOPoint(10, 50)));
    EXPECT_TRUE(r.contains(GEOPoint(30, 20)));

    // 右/上边界线上的点 = 不包含（high-right 角点不包含）
    EXPECT_FALSE(r.contains(GEOPoint(50, 90)));
    EXPECT_FALSE(r.contains(GEOPoint(50, 50)));
    EXPECT_FALSE(r.contains(GEOPoint(30, 90)));

    // 内部包含 / 外部排除
    EXPECT_TRUE(r.contains(GEOPoint(30, 50)));
    EXPECT_FALSE(r.contains(GEOPoint(9, 50)));
    EXPECT_FALSE(r.contains(GEOPoint(51, 50)));
    EXPECT_FALSE(r.contains(GEOPoint(30, 19)));
    EXPECT_FALSE(r.contains(GEOPoint(30, 91)));
}

// —— GEORect：半开区间 overlaps 边界用例 ——
TEST(GEORectTest, OverlapsHalfOpenBoundaries) {
    GEORect base(0, 0, 10, 10);

    // 部分交叠
    EXPECT_TRUE(overlaps(base, GEORect(5, 5, 15, 15)));
    // 完全包含
    EXPECT_TRUE(overlaps(base, GEORect(2, 2, 8, 8)));

    // 相邻矩形（共享一条边界线）不算交叠：右邻 / 上邻 / 对角
    EXPECT_FALSE(overlaps(base, GEORect(10, 0, 20, 10)));
    EXPECT_FALSE(overlaps(base, GEORect(0, 10, 10, 20)));
    EXPECT_FALSE(overlaps(base, GEORect(10, 10, 20, 20)));

    // 分离
    EXPECT_FALSE(overlaps(base, GEORect(20, 20, 30, 30)));
    // 交叠对称
    GEORect other(5, 5, 15, 15);
    EXPECT_TRUE(overlaps(other, base));
}

// —— GEOPolygon：bbox ——
TEST(GEOPolygonTest, BboxCoversAllVertices) {
    GEOPolygon poly{GEOPoint(5, 3), GEOPoint(-4, 8),
                            GEOPoint(2, -6), GEOPoint(7, 1)};

    GEORect bb = poly.bbox();
    EXPECT_EQ(bb.get_x_low(), -4);
    EXPECT_EQ(bb.get_y_low(), -6);
    EXPECT_EQ(bb.get_x_high(), 7);
    EXPECT_EQ(bb.get_y_high(), 8);

    // 顶点归属逐点断言（半开区间语义）：非排除边界上的顶点包含；
    // (7,1) 落在右边界 x=7 上、(-4,8) 落在上边界 y=8 上——半开区间
    // 排除上界，contains 为 false 是预期行为
    EXPECT_TRUE(bb.contains(GEOPoint(5, 3)));
    EXPECT_FALSE(bb.contains(GEOPoint(-4, 8)));
    EXPECT_TRUE(bb.contains(GEOPoint(2, -6)));
    EXPECT_FALSE(bb.contains(GEOPoint(7, 1)));
    // 边界上的顶点仍在 bbox 范围坐标上（bbox 计算正确性的另一面验证）
    EXPECT_EQ(bb.get_x_high(), 7);
    EXPECT_EQ(bb.get_y_high(), 8);

    // 单点多边形：退化包围盒（零宽零高）；单点自身落在半开区间排除
    // 边界上，contains 为 false——仅断言 bbox 坐标等于该点且面积为零
    GEOPolygonT<double> degenerate{GEOPointT<double>(1.5, 2.5)};
    GEORectT<double> dbb = degenerate.bbox();
    EXPECT_DOUBLE_EQ(dbb.area_double(), 0.0);
    EXPECT_DOUBLE_EQ(dbb.get_x_low(), 1.5);
    EXPECT_DOUBLE_EQ(dbb.get_y_low(), 2.5);
    EXPECT_DOUBLE_EQ(dbb.get_x_high(), 1.5);
    EXPECT_DOUBLE_EQ(dbb.get_y_high(), 2.5);
}

TEST(GEOPolygonTest, PropertyAccessors) {
    GEOPolygon poly{GEOPoint(0, 0), GEOPoint(4, 4)};
    ASSERT_EQ(poly.get_points().size(), 2u);

    // get_ref 原地修改点集
    poly.get_ref_points().push_back(GEOPoint(8, 2));
    EXPECT_EQ(poly.get_points().size(), 3u);

    // set 拷贝：源保持完整
    GEOPolygon target;
    target.set_points(poly.get_points());
    poly.get_ref_points().clear();
    EXPECT_EQ(target.get_points().size(), 3u);
    EXPECT_TRUE(poly.get_points().empty());
}

// —— 序列化往返：三类型 × {int32, int64, double} ——

// 浮点用 EXPECT_DOUBLE_EQ（位模式往返应精确相等），整数用 EXPECT_EQ。
template<typename T>
void expect_coord_eq(T a, T b) {
    if constexpr (std::is_integral_v<T>) {
        EXPECT_EQ(a, b);
    } else {
        EXPECT_DOUBLE_EQ(a, b);
    }
}

template<typename T>
void roundtrip_point(const GEOPointT<T>& v) {
    CMString blob;
    FLY_ENCODE(v, blob);
    GEOPointT<T> back;
    FLY_DECODE(blob, GEOPointT<T>, back);
    expect_coord_eq(back.get_x(), v.get_x());
    expect_coord_eq(back.get_y(), v.get_y());
}

template<typename T>
void roundtrip_rect(const GEORectT<T>& v) {
    CMString blob;
    FLY_ENCODE(v, blob);
    GEORectT<T> back;
    FLY_DECODE(blob, GEORectT<T>, back);
    expect_coord_eq(back.get_x_low(), v.get_x_low());
    expect_coord_eq(back.get_y_low(), v.get_y_low());
    expect_coord_eq(back.get_x_high(), v.get_x_high());
    expect_coord_eq(back.get_y_high(), v.get_y_high());
    // 往返后半开区间语义一致
    EXPECT_EQ(back.contains(v.low_left()), v.contains(v.low_left()));
    EXPECT_EQ(back.contains(v.high_right()), v.contains(v.high_right()));
}

template<typename T>
void roundtrip_polygon(const GEOPolygonT<T>& v) {
    CMString blob;
    FLY_ENCODE(v, blob);
    GEOPolygonT<T> back;
    FLY_DECODE(blob, GEOPolygonT<T>, back);

    ASSERT_EQ(back.get_points().size(), v.get_points().size());
    for (size_t i = 0; i < v.get_points().size(); ++i) {
        expect_coord_eq(back.get_points()[i].get_x(), v.get_points()[i].get_x());
        expect_coord_eq(back.get_points()[i].get_y(), v.get_points()[i].get_y());
    }
    // 往返后 bbox 一致
    GEORectT<T> src_bb = v.bbox();
    GEORectT<T> back_bb = back.bbox();
    expect_coord_eq(back_bb.get_x_low(), src_bb.get_x_low());
    expect_coord_eq(back_bb.get_y_high(), src_bb.get_y_high());
}

TEST(GeometrySerializeTest, PointRoundTripAllInstantiations) {
    roundtrip_point(GEOPoint(-5, 7));
    roundtrip_point(GEOPointT<int64_t>(-5000000000LL, 5000000000LL));
    roundtrip_point(GEOPointT<double>(-1.25, 3.75));
    // 默认构造（零值）往返
    roundtrip_point(GEOPoint());
    roundtrip_point(GEOPointT<double>());
}

TEST(GeometrySerializeTest, RectRoundTripAllInstantiations) {
    roundtrip_rect(GEORect(-100, -200, 300, 400));
    roundtrip_rect(GEORectT<int64_t>(-5000000000LL, -6000000000LL, 7000000000LL, 8000000000LL));
    roundtrip_rect(GEORectT<double>(0.5, -2.25, 10.75, 4.5));
    roundtrip_rect(GEORect());
    roundtrip_rect(GEORectT<double>());
}

TEST(GeometrySerializeTest, PolygonRoundTripAllInstantiations) {
    roundtrip_polygon(GEOPolygon{GEOPoint(0, 0), GEOPoint(10, 0),
                                         GEOPoint(5, 8)});
    roundtrip_polygon(GEOPolygonT<int64_t>{GEOPointT<int64_t>(-3, -4), GEOPointT<int64_t>(9, 12)});
    roundtrip_polygon(GEOPolygonT<double>{GEOPointT<double>(0.5, 0.25), GEOPointT<double>(2.5, 0.25),
                                        GEOPointT<double>(1.5, 3.75)});
}

}  // namespace
