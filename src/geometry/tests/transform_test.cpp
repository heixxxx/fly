// GEOTransform 单测（R1，方案 design-db-phase2-plan.md §3.1/§3.5）：
//   1. 八 orient 纯旋转公式逐条断言（§3.5.2 官方图示矢量级核对表，
//      与 OpenDB dbTransform::apply 同源）；
//   2. apply_box：W/E/FW/FE 宽高交换 + 角点 min/max 归一化、
//      N/FN/FS/S 宽高不变；
//   3. apply_polygon：逐顶点变换保持顶点序；
//   4. compose：全部 64 对组合的同态性质断言
//      compose(a,b).apply(p) == a.apply(b.apply(p)) + D4 群锚定值
//      （OpenDB orientMul 同源）；
//   5/6. 数值锚定 A/B：两张八方向配图（design-nested-def-placement.png /
//      design-lef-origin-placement.png）的 pos 全量期望值 + 放置语义
//      （变换后放置边界左下角 == DEF placement 点 t）；
//   7. FLY_SERIALIZE 序列化往返 × {int32_t, int64_t, double} 实例化。
#include <geometry/cpp/transform.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace {

using namespace fly;

// 全部八方向（按 GEOOrientation 枚举值序，与 Si2 DEF_ORIENT_* 一致）
constexpr GEOOrientation kAllOrients[8] = {
    GEOOrientation::N,  GEOOrientation::W,  GEOOrientation::S, GEOOrientation::E,
    GEOOrientation::FN, GEOOrientation::FW, GEOOrientation::FS, GEOOrientation::FE};

// —— 0. 枚举值对齐 Si2 defiNet.h 的 DEF_ORIENT_*（零映射直转的前提）——
TEST(GEOOrientationTest, ValuesAlignedWithDefiOrient) {
    static_assert(static_cast<uint8_t>(GEOOrientation::N) == 0);
    static_assert(static_cast<uint8_t>(GEOOrientation::W) == 1);
    static_assert(static_cast<uint8_t>(GEOOrientation::S) == 2);
    static_assert(static_cast<uint8_t>(GEOOrientation::E) == 3);
    static_assert(static_cast<uint8_t>(GEOOrientation::FN) == 4);
    static_assert(static_cast<uint8_t>(GEOOrientation::FW) == 5);
    static_assert(static_cast<uint8_t>(GEOOrientation::FS) == 6);
    static_assert(static_cast<uint8_t>(GEOOrientation::FE) == 7);
    // FW 在 FS 之前（与 Si2 头文件一致，易错点）
    EXPECT_LT(static_cast<uint8_t>(GEOOrientation::FW),
              static_cast<uint8_t>(GEOOrientation::FS));
}

// —— 1. 八 orient 纯旋转公式（§3.1 表，非对称点 (3,7)）——
TEST(GEOOrientationTest, PureRotationFormulas) {
    // 纯旋转 = 零偏移 transform 的 apply
    const GEOTransform zero;  // offset (0,0) + N
    const GEOPoint p(3, 7);

    struct Case {
        GEOOrientation o;
        int32_t x, y;
    };
    // N:(x,y) W:(−y,x) S:(−x,−y) E:(y,−x) FN:(−x,y) FS:(x,−y) FW:(y,x) FE:(−y,−x)
    const Case cases[] = {
        {GEOOrientation::N, 3, 7},    {GEOOrientation::W, -7, 3},
        {GEOOrientation::S, -3, -7},  {GEOOrientation::E, 7, -3},
        {GEOOrientation::FN, -3, 7},  {GEOOrientation::FS, 3, -7},
        {GEOOrientation::FW, 7, 3},   {GEOOrientation::FE, -7, -3},
    };
    for (const Case& c : cases) {
        const GEOPoint r =
            GEOTransform(GEOPoint(0, 0), c.o).apply(p);
        EXPECT_EQ(r.get_x(), c.x) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.get_y(), c.y) << "orient=" << static_cast<int>(c.o);
    }
    // 恒等基准（零偏移 transform 默认 N）自检
    EXPECT_EQ(zero.apply(p).get_x(), 3);
    EXPECT_EQ(zero.apply(p).get_y(), 7);
}

// —— 2. apply_box：宽高交换 + 角点归一化 ——
TEST(GEOTransformBoxTest, SwapDimensionsUnderW_E_FW_FE) {
    // 非对称矩形：宽 40 × 高 70（box 语义 x_high>=x_low）
    const GEORect box(10, 20, 50, 90);

    struct Case {
        GEOOrientation o;
        int32_t xl, yl, xh, yh;  // 变换后归一化角点（纯旋转）
    };
    const Case cases[] = {
        {GEOOrientation::W, -90, 10, -20, 50},
        {GEOOrientation::E, 20, -50, 90, -10},
        {GEOOrientation::FW, 20, 10, 90, 50},
        {GEOOrientation::FE, -90, -50, -20, -10},
    };
    for (const Case& c : cases) {
        const GEORect r =
            GEOTransform(GEOPoint(0, 0), c.o).apply_box(box);
        EXPECT_EQ(r.get_x_low(), c.xl) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.get_y_low(), c.yl) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.get_x_high(), c.xh) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.get_y_high(), c.yh) << "orient=" << static_cast<int>(c.o);
        // 宽高换轴：40×70 → 70×40
        EXPECT_EQ(r.width(), 70) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.height(), 40) << "orient=" << static_cast<int>(c.o);
    }
}

TEST(GEOTransformBoxTest, KeepDimensionsUnderN_FN_FS_S) {
    const GEORect box(10, 20, 50, 90);

    struct Case {
        GEOOrientation o;
        int32_t xl, yl, xh, yh;
    };
    const Case cases[] = {
        {GEOOrientation::N, 10, 20, 50, 90},
        {GEOOrientation::S, -50, -90, -10, -20},
        {GEOOrientation::FN, -50, 20, -10, 90},
        {GEOOrientation::FS, 10, -90, 50, -20},
    };
    for (const Case& c : cases) {
        const GEORect r =
            GEOTransform(GEOPoint(0, 0), c.o).apply_box(box);
        EXPECT_EQ(r.get_x_low(), c.xl) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.get_y_low(), c.yl) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.get_x_high(), c.xh) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.get_y_high(), c.yh) << "orient=" << static_cast<int>(c.o);
        // 宽高不变：40×70
        EXPECT_EQ(r.width(), 40) << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(r.height(), 70) << "orient=" << static_cast<int>(c.o);
    }
}

TEST(GEOTransformBoxTest, OffsetTranslatesAfterRotation) {
    // offset 在旋转之后叠加：apply_box = pos + R(box)
    const GEORect box(10, 20, 50, 90);
    const GEOTransform t(GEOPoint(1000, 2000),
                                  GEOOrientation::W);
    // 纯旋转结果 (−90,10)-(−20,50)，平移 (1000,2000)
    const GEORect r = t.apply_box(box);
    EXPECT_EQ(r.get_x_low(), 910);
    EXPECT_EQ(r.get_y_low(), 2010);
    EXPECT_EQ(r.get_x_high(), 980);
    EXPECT_EQ(r.get_y_high(), 2050);
}

// —— 3. apply_polygon：逐顶点变换、顶点序保持 ——
TEST(GEOTransformPolygonTest, KeepsVertexOrderUnderW) {
    // L 形多边形（顶点序即输入序）
    const GEOPolygon l_shape{GEOPoint(0, 0),
                                      GEOPoint(20, 0),
                                      GEOPoint(20, 10),
                                      GEOPoint(10, 10),
                                      GEOPoint(10, 20),
                                      GEOPoint(0, 20)};

    const GEOTransform t(GEOPoint(5, -3),
                                  GEOOrientation::W);
    const GEOPolygon r = t.apply_polygon(l_shape);

    ASSERT_EQ(r.get_points().size(), l_shape.get_points().size());
    // 各顶点 == 逐点 apply 结果（顶点序保持；含 offset (5,−3)）
    for (size_t i = 0; i < l_shape.get_points().size(); ++i) {
        const GEOPoint expect = t.apply(l_shape.get_points()[i]);
        EXPECT_EQ(r.get_points()[i].get_x(), expect.get_x()) << "i=" << i;
        EXPECT_EQ(r.get_points()[i].get_y(), expect.get_y()) << "i=" << i;
    }
    // 首顶点：(0,0) → R_W=(0,0) → +offset=(5,−3)
    EXPECT_EQ(r.get_points()[0].get_x(), 5);
    EXPECT_EQ(r.get_points()[0].get_y(), -3);
    // bbox == 逐顶点变换后的包围盒（W 下换轴：20×20 L 形仍 20×20，
    // 经 W + offset(5,−3)：x ∈ [5−20, 5]，y ∈ [−3, −3+20]）
    EXPECT_EQ(r.bbox().get_x_low(), -15);
    EXPECT_EQ(r.bbox().get_y_low(), -3);
    EXPECT_EQ(r.bbox().get_x_high(), 5);
    EXPECT_EQ(r.bbox().get_y_high(), 17);
}

// —— 4. compose：64 对全组合同态性质 + D4 群锚定值 ——
TEST(GEOTransformComposeTest, All64PairsHomomorphism) {
    // 非零 offset 才能锚定 offset 复合（offset = apply(sub.offset)）
    const GEOTransform a(GEOPoint(100, 50),
                                  GEOOrientation::N);  // orient 逐对外层替换
    const GEOTransform b(GEOPoint(-30, 20),
                                  GEOOrientation::N);
    // 多个非对称测试点
    const GEOPoint points[] = {
        GEOPoint(3, 7), GEOPoint(-5, 2),
        GEOPoint(11, -9), GEOPoint(0, 0)};

    for (uint8_t ao = 0; ao < 8; ++ao) {
        for (uint8_t bo = 0; bo < 8; ++bo) {
            const GEOTransform outer(
                GEOPoint(100, 50), kAllOrients[ao]);
            const GEOTransform inner(
                GEOPoint(-30, 20), kAllOrients[bo]);
            const GEOTransform both = outer.compose(inner);
            // orient 复合闭包：结果仍在八方向集合内
            const uint8_t co = static_cast<uint8_t>(both.get_orient());
            ASSERT_LT(co, 8) << "compose(" << int(ao) << "," << int(bo) << ")";
            for (const GEOPoint& p : points) {
                const GEOPoint lhs = both.apply(p);
                const GEOPoint rhs =
                    outer.apply(inner.apply(p));
                EXPECT_EQ(lhs.get_x(), rhs.get_x())
                    << "compose(" << int(ao) << "," << int(bo) << ") p=("
                    << p.get_x() << "," << p.get_y() << ")";
                EXPECT_EQ(lhs.get_y(), rhs.get_y())
                    << "compose(" << int(ao) << "," << int(bo) << ") p=("
                    << p.get_x() << "," << p.get_y() << ")";
            }
        }
    }
}

TEST(GEOTransformComposeTest, AnchoredCompositionValues) {
    // D4 群乘法锚定值（OpenDB orientMul 同源；§3.1 compose 语义 = 父∘子）
    struct Case {
        GEOOrientation a, b, expect;
    };
    const Case cases[] = {
        {GEOOrientation::W, GEOOrientation::FN, GEOOrientation::FE},
        {GEOOrientation::FS, GEOOrientation::FS, GEOOrientation::N},
        {GEOOrientation::W, GEOOrientation::W, GEOOrientation::S},
        {GEOOrientation::FW, GEOOrientation::FW, GEOOrientation::N},
    };
    for (const Case& c : cases) {
        const GEOTransform a(GEOPoint(0, 0), c.a);
        const GEOTransform b(GEOPoint(0, 0), c.b);
        EXPECT_EQ(a.compose(b).get_orient(), c.expect)
            << "compose(" << static_cast<int>(c.a) << ","
            << static_cast<int>(c.b) << ")";
    }
    // 单位元：compose(N,x) == x、compose(x,N) == x（全八方向）
    for (GEOOrientation o : kAllOrients) {
        const GEOTransform n(GEOPoint(1, 2),
                                      GEOOrientation::N);
        const GEOTransform x(GEOPoint(3, 4), o);
        EXPECT_EQ(n.compose(x).get_orient(), o);
        EXPECT_EQ(x.compose(n).get_orient(), o);
        // offset 复合公式直接验证：offset = apply(sub.offset)
        // n = N + (1,2)：compose(n,x).offset = x.offset + (1,2)
        EXPECT_EQ(n.compose(x).get_offset().get_x(),
                  x.get_offset().get_x() + 1);
        EXPECT_EQ(n.compose(x).get_offset().get_y(),
                  x.get_offset().get_y() + 2);
        // compose(x,n).offset = R(o)·(1,2) + x.offset
        const GEOPoint rn =
            GEOTransform(GEOPoint(0, 0), o)
                .apply(GEOPoint(1, 2));
        EXPECT_EQ(x.compose(n).get_offset().get_x(),
                  x.get_offset().get_x() + rn.get_x());
        EXPECT_EQ(x.compose(n).get_offset().get_y(),
                  x.get_offset().get_y() + rn.get_y());
    }
}

// —— 5. 数值锚定 A：嵌套 DEF 配图（design-nested-def-placement.png）——
// 子 DEF DIEAREA (100,200)-(500,480)（左下角非 (0,0)），block cell 放置
// 边界 box = diearea 矩形；主 DEF `- u1 SUBBLOCK + (1000,600) <orient>`。
// pos = t − apply_box(orient, box).ll()（红点 = 子 DEF (0,0) 的全局落点）。
TEST(GEOTransformPlacementTest, NestedDefAnchorAllOrients) {
    const GEORect box(100, 200, 500, 480);  // 400×280
    const GEOPoint t(1000, 600);

    struct Case {
        GEOOrientation o;
        int32_t px, py;  // pos 期望值（配图红点）
        bool swap;       // W/E/FW/FE 下宽高换轴
    };
    const Case cases[] = {
        {GEOOrientation::N, 900, 400, false},
        {GEOOrientation::W, 1480, 500, true},
        {GEOOrientation::S, 1500, 1080, false},
        {GEOOrientation::E, 800, 1100, true},
        {GEOOrientation::FN, 1500, 400, false},
        {GEOOrientation::FS, 900, 1080, false},
        {GEOOrientation::FW, 800, 500, true},
        {GEOOrientation::FE, 1480, 1100, true},
    };
    for (const Case& c : cases) {
        // t → pos 换算（design 适配层同一行公式）
        const GEORect rotated =
            GEOTransform(GEOPoint(0, 0), c.o).apply_box(box);
        const GEOPoint pos(t.get_x() - rotated.get_x_low(),
                                    t.get_y() - rotated.get_y_low());
        EXPECT_EQ(pos.get_x(), c.px)
            << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(pos.get_y(), c.py)
            << "orient=" << static_cast<int>(c.o);

        // 放置语义：pos/orient 二元组变换后放置边界左下角 == t
        const GEOTransform inst(pos, c.o);
        const GEORect placed = inst.apply_box(box);
        EXPECT_EQ(placed.get_x_low(), t.get_x())
            << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(placed.get_y_low(), t.get_y())
            << "orient=" << static_cast<int>(c.o);
        // 宽高换轴
        EXPECT_EQ(placed.width(), c.swap ? 280 : 400)
            << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(placed.height(), c.swap ? 400 : 280)
            << "orient=" << static_cast<int>(c.o);
    }
}

// —— 6. 数值锚定 B：LEF ORIGIN 配图（design-lef-origin-placement.png）——
// MACRO SIZE 800×400、ORIGIN (200,0) → 放置边界 box = [−origin_, −origin_+
// (W,H)] = (−200,0)-(600,400)；主 DEF `- i1 INV + (1000,600) <orient>`。
TEST(GEOTransformPlacementTest, LefOriginAnchorAllOrients) {
    const GEORect box(-200, 0, 600, 400);  // 800×400
    const GEOPoint t(1000, 600);

    struct Case {
        GEOOrientation o;
        int32_t px, py;
        bool swap;
    };
    const Case cases[] = {
        {GEOOrientation::N, 1200, 600, false},
        {GEOOrientation::W, 1400, 800, true},
        {GEOOrientation::S, 1600, 1000, false},
        {GEOOrientation::E, 1000, 1200, true},
        {GEOOrientation::FN, 1600, 600, false},
        {GEOOrientation::FS, 1200, 1000, false},
        {GEOOrientation::FW, 1000, 800, true},
        {GEOOrientation::FE, 1400, 1200, true},
    };
    for (const Case& c : cases) {
        const GEORect rotated =
            GEOTransform(GEOPoint(0, 0), c.o).apply_box(box);
        const GEOPoint pos(t.get_x() - rotated.get_x_low(),
                                    t.get_y() - rotated.get_y_low());
        EXPECT_EQ(pos.get_x(), c.px)
            << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(pos.get_y(), c.py)
            << "orient=" << static_cast<int>(c.o);

        const GEOTransform inst(pos, c.o);
        const GEORect placed = inst.apply_box(box);
        EXPECT_EQ(placed.get_x_low(), t.get_x())
            << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(placed.get_y_low(), t.get_y())
            << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(placed.width(), c.swap ? 400 : 800)
            << "orient=" << static_cast<int>(c.o);
        EXPECT_EQ(placed.height(), c.swap ? 800 : 400)
            << "orient=" << static_cast<int>(c.o);
    }
}

// —— 7. 序列化往返 × {int32_t, int64_t, double} 实例化 ——
template<typename T>
void expect_coord_eq(T a, T b) {
    if constexpr (std::is_integral_v<T>) {
        EXPECT_EQ(a, b);
    } else {
        EXPECT_DOUBLE_EQ(a, b);
    }
}

template<typename T>
void roundtrip_transform(const GEOTransformT<T>& v) {
    CMString blob;
    FLY_ENCODE(v, blob);
    GEOTransformT<T> back;
    FLY_DECODE(blob, GEOTransformT<T>, back);

    expect_coord_eq(back.get_offset().get_x(), v.get_offset().get_x());
    expect_coord_eq(back.get_offset().get_y(), v.get_offset().get_y());
    EXPECT_EQ(back.get_orient(), v.get_orient());
}

TEST(GEOTransformSerializeTest, RoundTripAllInstantiations) {
    roundtrip_transform(
        GEOTransform(GEOPoint(1000, 600), GEOOrientation::FW));
    roundtrip_transform(GEOTransformT<int64_t>(
        GEOPointT<int64_t>(-5000000000LL, 6000000000LL), GEOOrientation::FE));
    roundtrip_transform(
        GEOTransformT<double>(GEOPointT<double>(0.5, -2.25), GEOOrientation::FN));
    // 默认构造（零偏移 + N）往返
    roundtrip_transform(GEOTransform());
    roundtrip_transform(GEOTransformT<double>());
}

TEST(GEOTransformSerializeTest, OffsetAndOrientSurviveRoundTrip) {
    // 往返后变换行为不变（不只是字段值）
    const GEOTransform src(GEOPoint(100, 50),
                                    GEOOrientation::W);
    CMString blob;
    FLY_ENCODE(src, blob);
    GEOTransform back;
    FLY_DECODE(blob, GEOTransform, back);

    const GEOPoint p(-7, 3);
    EXPECT_EQ(back.apply(p).get_x(), src.apply(p).get_x());
    EXPECT_EQ(back.apply(p).get_y(), src.apply(p).get_y());
    // compose 行为一致（orient 表驱动，往返后复合结果不变）
    const GEOTransform sub(GEOPoint(-30, 20),
                                    GEOOrientation::FN);
    EXPECT_EQ(back.compose(sub).get_orient(), src.compose(sub).get_orient());
}

}  // namespace
