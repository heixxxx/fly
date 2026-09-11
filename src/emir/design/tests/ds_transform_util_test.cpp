// R6 transform 业务接入单测：place_from_def 数值锚定 + DSInstance 结构。
//
// 锚定来源：两张八方向配图（design-nested-def-placement.png /
// design-lef-origin-placement.png）的 pos 全量期望值——与 geometry 模块
// transform_test.cpp 的锚定 A/B 同数值，但经 DSCell 字段路径构造
//（bbox_/origin_ 来自 cell 而非手拼 box），覆盖两类 cell 构造：
//   - block cell（P7：origin_ = −diearea 左下角、bbox_ = DIEAREA 原坐标）；
//   - LEF cell（origin_ = ORIGIN 语句值、bbox_ = (0,0)-(SIZE)）。
// 另锚定 §3.3 使用示例 1（std cell INV + ORIGIN 非 0 + pin 全局坐标）。
// 序列化：DSInstance FLY_SERIALIZE 往返。
#include <emir/design/cpp/ds_transform_util.h>
#include <emir/design/cpp/ds_types.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using namespace fly;

// 全部八方向（defin 回调 orient 整型直转的输入域）
constexpr int kAllOrientInts[8] = {0, 1, 2, 3, 4, 5, 6, 7};

// —— 锚定 A（经 DSCell 路径）：嵌套 DEF 配图 ——
// 子 DEF DIEAREA (100,200)-(500,480)（左下角非 (0,0)）；P7 block cell：
// bbox_ = diearea 原坐标、origin_ = −diearea_ll = (−100,−200)。
// 主 DEF `- u1 SUBBLOCK + (1000,600) <orient>`。
TEST(PlaceFromDefTest, NestedDefBlockCellAllOrients) {
    DSCell blk;
    blk.set_name("SUBBLOCK");
    blk.set_block_cell();
    blk.set_bbox(GEORect(100, 200, 500, 480));
    blk.set_origin_x(-100);
    blk.set_origin_y(-200);

    const GEOPoint t(1000, 600);
    struct Case {
        int orient_int;
        int32_t px, py;  // pos 期望值（配图红点 = 子 DEF (0,0) 的全局落点）
    };
    const Case cases[] = {
        {0, 900, 400},    // N
        {1, 1480, 500},   // W
        {2, 1500, 1080},  // S
        {3, 800, 1100},   // E
        {4, 1500, 400},   // FN
        {5, 800, 500},    // FW
        {6, 900, 1080},   // FS
        {7, 1480, 1100},  // FE
    };
    for (const Case& c : cases) {
        const GEOTransform inst =
            place_from_def(t, c.orient_int, blk);
        EXPECT_EQ(inst.get_offset().get_x(), c.px)
            << "orient_int=" << c.orient_int;
        EXPECT_EQ(inst.get_offset().get_y(), c.py)
            << "orient_int=" << c.orient_int;
        // orient 直转零映射（defin 回调整型 == GEOOrientation 枚举值）
        EXPECT_EQ(static_cast<int>(inst.get_orient()), c.orient_int)
            << "orient_int=" << c.orient_int;

        // 放置语义回验：transform 作用后放置边界（= diearea）左下角 == t
        const GEORect placed = inst.apply_box(blk.get_bbox());
        EXPECT_EQ(placed.get_x_low(), t.get_x())
            << "orient_int=" << c.orient_int;
        EXPECT_EQ(placed.get_y_low(), t.get_y())
            << "orient_int=" << c.orient_int;
    }
}

// —— 锚定 B（经 DSCell 路径）：LEF ORIGIN 配图 ——
// MACRO SIZE 800×400、ORIGIN (200,0)：bbox_ = (0,0)-(800,400)、
// origin_ = (200,0)。主 DEF `- i1 INV + (1000,600) <orient>`。
TEST(PlaceFromDefTest, LefOriginCellAllOrients) {
    DSCell inv;
    inv.set_name("INV");
    inv.set_lef_cell();
    inv.set_bbox(GEORect(0, 0, 800, 400));
    inv.set_origin_x(200);
    inv.set_origin_y(0);

    const GEOPoint t(1000, 600);
    struct Case {
        int orient_int;
        int32_t px, py;  // pos 期望值（配图红点 = LEF 宏 (0,0) 的全局落点）
    };
    const Case cases[] = {
        {0, 1200, 600},   // N
        {1, 1400, 800},   // W
        {2, 1600, 1000},  // S
        {3, 1000, 1200},  // E
        {4, 1600, 600},   // FN
        {5, 1000, 800},   // FW
        {6, 1200, 1000},  // FS
        {7, 1400, 1200},  // FE
    };
    for (const Case& c : cases) {
        const GEOTransform inst =
            place_from_def(t, c.orient_int, inv);
        EXPECT_EQ(inst.get_offset().get_x(), c.px)
            << "orient_int=" << c.orient_int;
        EXPECT_EQ(inst.get_offset().get_y(), c.py)
            << "orient_int=" << c.orient_int;

        // 放置语义回验：变换后放置边界（SIZE 框 −ORIGIN）左下角 == t
        const GEORect box(-200, 0, 600, 400);
        const GEORect placed = inst.apply_box(box);
        EXPECT_EQ(placed.get_x_low(), t.get_x())
            << "orient_int=" << c.orient_int;
        EXPECT_EQ(placed.get_y_low(), t.get_y())
            << "orient_int=" << c.orient_int;
    }
}

// —— §3.3 使用示例 1（DBU@1000）：std cell INV + ORIGIN (0.1,0) 非 0，
// pin 全局坐标断言（文档示例的 µm 值 ×1000）。
TEST(PlaceFromDefTest, DocExampleStdCellWithPin) {
    DSCell inv;
    inv.set_bbox(GEORect(0, 0, 800, 400));  // SIZE 0.8×0.4 µm
    inv.set_origin_x(100);                           // ORIGIN (0.1, 0)
    inv.set_origin_y(0);

    // DEF：- inst1 INV + (100, 200) W —— t = (100000, 200000) @1000 DBU
    const GEOTransform inst =
        place_from_def(GEOPoint(100000, 200000), 1, inv);
    // pos = (100.4, 200.1) µm —— LEF 宏 (0,0) 点全局位
    EXPECT_EQ(inst.get_offset().get_x(), 100400);
    EXPECT_EQ(inst.get_offset().get_y(), 200100);
    EXPECT_EQ(inst.get_orient(), GEOOrientation::W);

    // pin Z 图形 LEF 原值 (600,100)-(700,300)（不归一化原样存储）：
    // pin 全局 = R_W(pin) + pos → (100100,200700)-(100300,200800)
    const GEORect pin_global =
        inst.apply_box(GEORect(600, 100, 700, 300));
    EXPECT_EQ(pin_global.get_x_low(), 100100);
    EXPECT_EQ(pin_global.get_y_low(), 200700);
    EXPECT_EQ(pin_global.get_x_high(), 100300);
    EXPECT_EQ(pin_global.get_y_high(), 200800);
}

// —— 越界 orient 整型为调用方契约错误：枚举直转后非法值由 geometry 层
// debug 断言拦截（release 下行为 = switch 落空返回恒等，不产生未定义）。
// 此处只锚定合法域 0-7 直转零映射。
TEST(PlaceFromDefTest, OrientIntDirectCastZeroMapping) {
    DSCell cell;
    cell.set_bbox(GEORect(0, 0, 10, 20));
    for (int oi : kAllOrientInts) {
        const GEOTransform inst =
            place_from_def(GEOPoint(0, 0), oi, cell);
        EXPECT_EQ(static_cast<int>(inst.get_orient()), oi) << "oi=" << oi;
    }
}

// —— DSInstance 结构（⑧ local id 从 1 起、local 0 = block 自身占位——
// 分配语义在链/产物层，此处锚定字段与缺省值）+ 序列化往返。
// R7 ㊱ 类型层断言：DSInstance 不存 name（实例名仅在
// DSInstanceNameHasher 的 per-DEF local 名空间，随 DSBlockNames_<i>
// 伴生落盘）——SFINAE 探测 get_name 成员必须缺席。
namespace {
template <typename T, typename = void>
struct has_get_name : std::false_type {};
template <typename T>
struct has_get_name<T, std::void_t<decltype(std::declval<const T&>().get_name())>>
    : std::true_type {};
static_assert(!has_get_name<fly::DSInstance>::value,
              "DSInstance must not carry a name (㊱ name 存储分层)");
static_assert(!has_get_name<fly::DSPin>::value,
              "DSPin must not carry a name (㊱ name 存储分层)");
}  // namespace
TEST(DSInstanceTest, DefaultsAndSerializeRoundTrip) {
    DSInstance inst;
    EXPECT_EQ(inst.get_cell_id(), 0u);
    EXPECT_EQ(inst.get_transform().get_orient(), GEOOrientation::N);
    EXPECT_EQ(inst.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::UNPLACED));
    EXPECT_DOUBLE_EQ(inst.get_weight(), 0.0);  // OPTIONAL weight 缺省 0

    inst.set_cell_id(7);
    inst.set_transform(
        GEOTransform(GEOPoint(100400, 200100),
                              GEOOrientation::W));
    inst.set_placement_status(static_cast<uint8_t>(DSPlacementStatus::FIXED));
    inst.set_weight(2.5);

    CMString blob;
    FLY_ENCODE(inst, blob);
    DSInstance back;
    FLY_DECODE(blob, DSInstance, back);

    EXPECT_EQ(back.get_cell_id(), 7u);
    EXPECT_EQ(back.get_transform().get_offset().get_x(), 100400);
    EXPECT_EQ(back.get_transform().get_offset().get_y(), 200100);
    EXPECT_EQ(back.get_transform().get_orient(), GEOOrientation::W);
    EXPECT_EQ(back.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::FIXED));
    EXPECT_DOUBLE_EQ(back.get_weight(), 2.5);
}

}  // namespace
