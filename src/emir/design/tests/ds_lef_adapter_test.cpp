// LEF 解析适配层单测（S1 tech lef / S2 cell lef）：層表/DBU/制造网格、
// via 转 DSViaCell（cut/enclosure 经 stack 层型判定）、VIARULE GENERATE
// 展开为模板 DSViaCell（㉚）、implant 跳过计数、重名保留首份、简化 pin
// （USE/DIRECTION）、pin 几何入独立对象（R4 键 = 局部平铺 pin id）、OBS
// 入 cell、DBU 不一致 raise（D15）、层引用缺失 raise、语法错误 raise、
// 产物整体序列化往返。
// 数据：data/tech_synth.lef、data/cells_synth.lef、data/bad_syntax.lef
// （自制精简样例，DBU=2000；期望值人工核定写死）。
#include <emir/design/cpp/ds_lef_adapter.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

namespace {

namespace fs = std::filesystem;

using namespace fly;

// bitsery 不支持顶层容器直序、FLY_SERIALIZE 不可用于函数局部类——
// 集合往返经此包装 struct 承载
struct ViaList {
    CMVector<DSViaCell> vias_;
    FLY_SERIALIZE(vias_)
};

fs::path test_data(const char* name) {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace) {
        return fs::path(srcdir) / workspace / "src/emir/design/tests/data" /
               name;
    }
    return fs::path("data") / name;
}

TEST(DsTechLefTest, ParseStackViasAndViarules) {
    DSStack stack;
    CMVector<DSViaCell> vias;

    const DSLefParseStats stats =
        ds_parse_tech_lef(test_data("tech_synth.lef").string(), stack, vias);

    // 统计（人工核定：3 收录层 + 1 implant 跳过 + VIA12 定义两遍保留
    // 首份 + VIAGEN12 规则展开 1 模板 via）
    EXPECT_EQ(stats.layer_count, 3);
    EXPECT_EQ(stats.skipped_layer_count, 1);
    EXPECT_EQ(stats.via_count, 2);  // VIA12 定义 + VIAGEN12 展开（㉚）
    EXPECT_EQ(stats.via_conflict_count, 1);
    EXPECT_EQ(stats.viarule_count, 1);

    // DBU 基准与制造网格（S1 确立）
    EXPECT_EQ(stack.get_dbu_per_micron(), 2000);
    EXPECT_EQ(stack.get_manufacturing_grid(), 5);

    // 层堆叠顺序即索引序；几何属性换算核定（×2000，面积 ×2000²）
    ASSERT_EQ(stack.layer_count(), 3u);
    EXPECT_EQ(stack.layer_at(0).get_name(), "M1");
    EXPECT_EQ(stack.layer_at(0).get_type(),
              static_cast<uint8_t>(DSLayerType::ROUTING));
    EXPECT_EQ(stack.layer_at(0).get_direction(),
              static_cast<uint8_t>(DSDirection::HORIZONTAL));
    EXPECT_EQ(stack.layer_at(0).get_default_width(), 140);
    EXPECT_EQ(stack.layer_at(0).get_pitch(), 380);
    ASSERT_EQ(stack.layer_at(0).get_spacing().size(), 2u);
    EXPECT_EQ(stack.layer_at(0).get_spacing()[0], 140);
    EXPECT_EQ(stack.layer_at(0).get_spacing()[1], 180);
    EXPECT_EQ(stack.layer_at(1).get_name(), "VIA1");
    EXPECT_EQ(stack.layer_at(1).get_type(),
              static_cast<uint8_t>(DSLayerType::CUT));
    EXPECT_EQ(stack.layer_at(2).get_name(), "M2");
    EXPECT_EQ(stack.layer_at(2).get_direction(),
              static_cast<uint8_t>(DSDirection::VERTICAL));
    EXPECT_EQ(stack.layer_at(2).get_default_width(), 160);
    EXPECT_EQ(stack.layer_at(2).get_pitch(), 420);
    EXPECT_EQ(stack.layer_at(2).get_min_area(), 200000);

    // via：层名经 stack 转 id；cut/enclosure 按层型与 stack 序归属
    ASSERT_EQ(vias.size(), 2u);
    const DSViaCell& via = vias[0];
    EXPECT_EQ(via.get_name(), "VIA12");
    EXPECT_EQ(via.get_bottom_layer_id(), 0u);  // M1
    EXPECT_EQ(via.get_top_layer_id(), 2u);     // M2
    ASSERT_EQ(via.cut_rect_count(), 1u);
    EXPECT_EQ(via.cut_rect_at(0).get_x_low(), -100);
    EXPECT_EQ(via.cut_rect_at(0).get_y_high(), 100);
    ASSERT_EQ(via.bottom_enclosure_count(), 1u);
    EXPECT_EQ(via.bottom_enclosure_at(0).get_x_low(), -200);
    ASSERT_EQ(via.top_enclosure_count(), 1u);
    EXPECT_EQ(via.top_enclosure_at(0).get_x_high(), 240);

    // ㉚：VIARULE GENERATE 按规则默认参数展开为模板 DSViaCell（中心
    // 对齐：cut = 规则 RECT，enclosure = cut 四边外扩 overhang）
    // VIAGEN12：cut RECT ±0.05 → ±100；M1/M2 ENCLOSURE 0.03 0.04
    // → x 外扩 60、y 外扩 80 → bottom/top 各 (−160,−180)-(160,180)
    const DSViaCell& gen = vias[1];
    EXPECT_EQ(gen.get_name(), "VIAGEN12");
    EXPECT_EQ(gen.get_bottom_layer_id(), 0u);  // M1
    EXPECT_EQ(gen.get_top_layer_id(), 2u);     // M2
    ASSERT_EQ(gen.cut_rect_count(), 1u);
    EXPECT_EQ(gen.cut_rect_at(0).get_x_low(), -100);
    EXPECT_EQ(gen.cut_rect_at(0).get_y_high(), 100);
    ASSERT_EQ(gen.bottom_enclosure_count(), 1u);
    EXPECT_EQ(gen.bottom_enclosure_at(0).get_x_low(), -160);
    EXPECT_EQ(gen.bottom_enclosure_at(0).get_y_low(), -180);
    EXPECT_EQ(gen.bottom_enclosure_at(0).get_x_high(), 160);
    EXPECT_EQ(gen.bottom_enclosure_at(0).get_y_high(), 180);
    ASSERT_EQ(gen.top_enclosure_count(), 1u);
    EXPECT_EQ(gen.top_enclosure_at(0).get_x_low(), -160);
    EXPECT_EQ(gen.top_enclosure_at(0).get_y_high(), 180);
}

TEST(DsCellLefTest, ParseMacrosPinsGeometryAndVias) {
    DSStack stack;
    CMVector<DSViaCell> tech_vias;
    // ㉚：via_count 含 VIAGEN12 规则展开的模板 via（VIA 定义 + 展开 = 2）
    ASSERT_EQ(ds_parse_tech_lef(test_data("tech_synth.lef").string(), stack,
                                tech_vias)
                  .via_count,
              2);

    DSDesign design;
    DSPinGeometry pin_geoms;
    CMVector<DSViaCell> vias;
    const DSLefParseStats stats =
        ds_parse_cell_lef(test_data("cells_synth.lef").string(), stack, design,
                          pin_geoms, vias);

    // 统计（核定：3 macro 定义、第 3 个与首个重名保留首份；5 pin）
    EXPECT_EQ(stats.macro_count, 2);
    EXPECT_EQ(stats.skipped_macro_count, 1);
    EXPECT_EQ(stats.pin_count, 5);
    EXPECT_EQ(stats.via_count, 1);
    EXPECT_EQ(stats.via_conflict_count, 0);
    EXPECT_EQ(stats.viarule_count, 0);

    // DBU 一致时不 raise 且 cell 字段换算核定（㉞ bbox + 派生尺寸；
    // ㉗ lef_cell/macro_cell 来源标记）
    ASSERT_EQ(design.cells_.size(), 2u);
    const DSCell& inv = design.cells_[0];
    EXPECT_EQ(inv.get_name(), "INV_X1");
    EXPECT_EQ(inv.get_class(), "CORE");
    EXPECT_TRUE(inv.is_lef_cell());
    EXPECT_TRUE(inv.is_macro_cell());
    EXPECT_EQ(inv.width(), 1400);   // 0.7 µm × 2000（bbox 派生）
    EXPECT_EQ(inv.height(), 1400);
    EXPECT_EQ(inv.get_bbox().get_x_low(), 0);
    EXPECT_EQ(inv.get_bbox().get_y_high(), 1400);
    EXPECT_EQ(inv.get_origin_x(), 0);
    EXPECT_EQ(inv.get_site(), "site1");

    // 简化 pin（⑰）：USE POWER → power 类型、DIRECTION 映射。
    // R7 ㊱：DSPin 无 name——pin 名经 pin hasher 组合键反查（局部 pin id
    // = 平铺分配序，与下标一致）
    ASSERT_EQ(inv.pin_count(), 3u);
    EXPECT_EQ(design.pin_name_of(0), "A");
    EXPECT_EQ(inv.pin_at(0).get_type(),
              static_cast<uint8_t>(DSPinType::SIGNAL));
    EXPECT_EQ(inv.pin_at(0).get_direction(),
              static_cast<uint8_t>(DSPinDirection::INPUT));
    EXPECT_EQ(design.pin_name_of(1), "ZN");
    EXPECT_EQ(inv.pin_at(1).get_direction(),
              static_cast<uint8_t>(DSPinDirection::OUTPUT));
    EXPECT_EQ(design.pin_name_of(2), "VDD");
    EXPECT_EQ(inv.pin_at(2).get_type(),
              static_cast<uint8_t>(DSPinType::POWER));
    EXPECT_EQ(inv.pin_at(2).get_direction(),
              static_cast<uint8_t>(DSPinDirection::INOUT));

    // pin 几何入独立对象（R4：键 = 局部平铺 pin id，每 pin 一条目），
    // 层名 → layer id
    const CMVector<DSShapeRef>* a_geoms = pin_geoms.geometry_of(0);
    ASSERT_NE(a_geoms, nullptr);
    ASSERT_EQ(a_geoms->size(), 1u);  // A 一个 rect
    EXPECT_EQ((*a_geoms)[0].get_layer_id(), 0u);  // A 在 M1
    EXPECT_EQ((*a_geoms)[0].get_rect().get_x_high(), 200);
    const CMVector<DSShapeRef>* zn_geoms = pin_geoms.geometry_of(1);
    ASSERT_NE(zn_geoms, nullptr);
    EXPECT_EQ((*zn_geoms)[0].get_layer_id(), 2u);  // ZN 在 M2
    EXPECT_EQ((*zn_geoms)[0].get_rect().get_x_low(), 600);
    const CMVector<DSShapeRef>* vdd_geoms = pin_geoms.geometry_of(2);
    ASSERT_NE(vdd_geoms, nullptr);
    EXPECT_EQ((*vdd_geoms)[0].get_layer_id(), 0u);  // VDD 在 M1
    EXPECT_EQ((*vdd_geoms)[0].get_rect().get_y_low(), 1200);
    // DFF_X1 的 2 pin（D=3/Q=4，几何各自一条）
    ASSERT_NE(pin_geoms.geometry_of(3), nullptr);
    EXPECT_EQ(pin_geoms.geometry_of(3)->size(), 1u);
    ASSERT_NE(pin_geoms.geometry_of(4), nullptr);
    EXPECT_EQ(pin_geoms.geometry_of(4)->size(), 1u);

    // OBS 几何入 cell（D19）
    ASSERT_EQ(inv.obs_count(), 2u);
    EXPECT_EQ(inv.obs_at(0).get_layer_id(), 0u);
    EXPECT_EQ(inv.obs_at(0).get_rect().get_x_low(), 200);
    EXPECT_EQ(inv.obs_at(1).get_layer_id(), 2u);
    EXPECT_EQ(inv.obs_at(1).get_rect().get_x_high(), 1200);

    // DFF_X1（无 SITE/无 OBS）
    const DSCell& dff = design.cells_[1];
    EXPECT_EQ(dff.get_name(), "DFF_X1");
    EXPECT_EQ(dff.width(), 2800);
    EXPECT_EQ(dff.get_site(), "");
    EXPECT_EQ(dff.obs_count(), 0u);

    // cell lef 侧 via 入文件集（跨文件同名合并 T6 汇总处理）
    ASSERT_EQ(vias.size(), 1u);
    EXPECT_EQ(vias[0].get_name(), "VIA12");

    // namemap 经 add_cell/register 语义可直接查询
    ASSERT_NE(design.find_cell("INV_X1"), nullptr);
    EXPECT_EQ(design.find_cell("INV_X1"), &design.cells_[0]);
    EXPECT_EQ(design.find_cell("DFF_X1"), &design.cells_[1]);

    // R4：S2 局部 pin namemap 注册（局部 id = part 内跨 cell 平铺；全局
    // 平铺 id 由 T6 汇总重排后回填）——ds_merge_cell_lef 重挂与 pin_id
    // 回填的数据源
    EXPECT_EQ(design.pin_names_.get_id("INV_X1/A"), 0u);
    EXPECT_EQ(design.pin_names_.get_id("INV_X1/ZN"), 1u);
    EXPECT_EQ(design.pin_names_.get_id("INV_X1/VDD"), 2u);
    EXPECT_EQ(design.pin_names_.get_id("DFF_X1/D"), 3u);
    EXPECT_EQ(design.pin_names_.get_id("DFF_X1/Q"), 4u);
    EXPECT_EQ(design.pin_names_.size(), 5u);
}

TEST(DsCellLefTest, DbuMismatchRaises) {
    // D15：cell lef 的 UNITS 与 stack 基准不一致 → raise
    DSStack stack;
    CMVector<DSViaCell> tech_vias;
    ds_parse_tech_lef(test_data("tech_synth.lef").string(), stack, tech_vias);
    stack.set_dbu_per_micron(1000);  // 篡改基准模拟不一致

    DSDesign design;
    DSPinGeometry pin_geoms;
    CMVector<DSViaCell> vias;
    EXPECT_THROW(ds_parse_cell_lef(test_data("cells_synth.lef").string(),
                                   stack, design, pin_geoms, vias),
                 std::runtime_error);
}

TEST(DsLefNegativeTest, UndefinedLayerReferenceRaises) {
    // VIA 引用 stack 中未定义的层 → raise（D15；tech/cell 两侧同一 helper）
    DSStack empty_stack;
    DSDesign design;
    DSPinGeometry pin_geoms;
    CMVector<DSViaCell> vias;
    EXPECT_THROW(ds_parse_cell_lef(test_data("cells_synth.lef").string(),
                                   empty_stack, design, pin_geoms, vias),
                 std::runtime_error);
}

TEST(DsLefNegativeTest, UnreadableAndSyntaxErrorRaise) {
    DSStack stack;
    CMVector<DSViaCell> vias;
    // 文件不可读（dev-rules §7 第一类）
    EXPECT_THROW(ds_parse_tech_lef("/nonexistent/no.lef", stack, vias),
                 std::runtime_error);
    // 语法错误（缺分号 → lefrRead 非 0）
    DSStack s2;
    EXPECT_THROW(ds_parse_tech_lef(test_data("bad_syntax.lef").string(), s2,
                                   vias),
                 std::runtime_error);
}

TEST(DsLefRoundTripTest, ProductsSerializeRoundTrip) {
    // 解析产物整体往返（衔接 T3 序列化能力）

    DSStack stack;
    CMVector<DSViaCell> tech_vias;
    ds_parse_tech_lef(test_data("tech_synth.lef").string(), stack, tech_vias);

    DSDesign design;
    DSPinGeometry pin_geoms;
    CMVector<DSViaCell> vias;
    ds_parse_cell_lef(test_data("cells_synth.lef").string(), stack, design,
                      pin_geoms, vias);

    // DSStack 往返
    CMString blob;
    FLY_ENCODE(stack, blob);
    DSStack stack_back;
    FLY_DECODE(blob, DSStack, stack_back);
    ASSERT_EQ(stack_back.layer_count(), 3u);
    EXPECT_EQ(stack_back.layer_at(0).get_default_width(), 140);
    EXPECT_EQ(stack_back.layer_at(2).get_min_area(), 200000);
    EXPECT_EQ(stack_back.find_layer("M2"), 2u);  // 惰性索引重建后可用

    // DSDesign 往返
    CMString design_blob;
    FLY_ENCODE(design, design_blob);
    DSDesign design_back;
    FLY_DECODE(design_blob, DSDesign, design_back);
    ASSERT_EQ(design_back.cells_.size(), 2u);
    EXPECT_EQ(design_back.cells_[0].get_name(), "INV_X1");
    EXPECT_EQ(design_back.cells_[0].pin_at(2).get_type(),
              static_cast<uint8_t>(DSPinType::POWER));
    EXPECT_EQ(design_back.cells_[0].obs_at(1).get_rect().get_x_high(), 1200);
    ASSERT_NE(design_back.find_cell("DFF_X1"), nullptr);

    // DSPinGeometry 往返（R4：键 = 局部平铺 pin id）
    CMString geom_blob;
    FLY_ENCODE(pin_geoms, geom_blob);
    DSPinGeometry geoms_back;
    FLY_DECODE(geom_blob, DSPinGeometry, geoms_back);
    ASSERT_NE(geoms_back.geometry_of(0), nullptr);
    EXPECT_EQ(geoms_back.geometry_of(0)->size(), 1u);  // INV_X1/A 一条
    EXPECT_EQ(geoms_back.geometry_of(0)->at(0).get_layer_id(), 0u);
    ASSERT_NE(geoms_back.geometry_of(4), nullptr);     // DFF_X1/Q
    EXPECT_EQ(geoms_back.geometry_of(4)->at(0).get_layer_id(), 2u);

    // DSViaCell 集合往返
    ViaList via_list{vias};
    CMString via_blob;
    FLY_ENCODE(via_list, via_blob);
    ViaList vias_back;
    FLY_DECODE(via_blob, ViaList, vias_back);
    ASSERT_EQ(vias_back.vias_.size(), 1u);
    EXPECT_EQ(vias_back.vias_[0].get_name(), "VIA12");
    EXPECT_EQ(vias_back.vias_[0].cut_rect_at(0).get_x_low(), -100);
}

}  // namespace
