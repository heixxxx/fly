// design 模块数据结构单测（T3）：序列化往返（DSStack/DSDesign/
// DSPinTables/DSPinGeometry 全字段）、运行时注入字段不序列化（⑱）、
// get_cell 指针注入零拷贝（⑰）、fake cell 结构就位（⑲/⑳）、
// namemap 双向一致性（②）、R4 pin 三字段按 pin 维度组织（DSPin 全局
// pin_id_/placement_status_、表/几何按全局 pin id 检索、cell 维度便利
// 聚合）。
#include <emir/design/cpp/ds_types.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

namespace {

using namespace fly;

// 构造一个 CMLookupTable（最简 1x2 自足模式）
CMLookupTable make_table(const char* name, double v0, double v1) {
    CMLookupTable t;
    t.name_ = name;
    t.dim_ = 1;
    t.variable_names_ = {"input_net_transition"};
    t.index_sets_ = {{0.0, 1.0}};
    t.values_ = {v0, v1};
    return t;
}

// 构造标准层堆叠：M1(routing/H) → V1(cut) → M2(routing/V)，DBU 2000
DSStack make_stack() {
    DSStack stack;
    stack.dbu_basis_ = "test_dbu";
    stack.dbu_per_micron_ = 2000;
    stack.manufacturing_grid_ = 5;

    DSLayer m1;
    m1.name_ = "M1";
    m1.type_ = static_cast<uint8_t>(DSLayerType::ROUTING);
    m1.direction_ = static_cast<uint8_t>(DSDirection::HORIZONTAL);
    m1.default_width_ = 140;
    m1.pitch_ = 380;
    m1.spacing_ = {140, 160};
    m1.min_area_ = 100000;
    stack.add_layer(std::move(m1));

    DSLayer v1;
    v1.name_ = "V1";
    v1.type_ = static_cast<uint8_t>(DSLayerType::CUT);
    stack.add_layer(std::move(v1));

    DSLayer m2;
    m2.name_ = "M2";
    m2.type_ = static_cast<uint8_t>(DSLayerType::ROUTING);
    m2.direction_ = static_cast<uint8_t>(DSDirection::VERTICAL);
    m2.default_width_ = 160;
    m2.pitch_ = 420;
    stack.add_layer(std::move(m2));
    return stack;
}

// 构造标准 design：2 cell（其一 fake）+ 2 via cell + 1 block cell（2
// port pin）。pin namemap 注册（D1 平铺 id）与 cell.pins_ 的 pin_id_
// 回填同步模拟（R4：建库链路 register_pin 分配全局 id 时回填
// DSPin::pin_id_）。
DSDesign make_design() {
    DSDesign design;

    DSCell inv;
    inv.name_ = "INV_X1";
    inv.set_bbox(GEORect(0, 0, 1400, 1400));
    inv.origin_x_ = 0;
    inv.origin_y_ = 0;
    inv.class_ = "CORE";
    inv.site_ = "site1";
    inv.set_lef_cell();
    inv.set_macro_cell();
    DSPin a;  // pin 名不存 DSPin（R7 ㊱）——经 register_pin 进全局 hasher
    a.type_ = static_cast<uint8_t>(DSPinType::SIGNAL);
    a.direction_ = static_cast<uint8_t>(DSPinDirection::INPUT);
    inv.add_pin(std::move(a));
    DSPin zn;
    zn.direction_ = static_cast<uint8_t>(DSPinDirection::OUTPUT);
    inv.add_pin(std::move(zn));
    DSShapeRef obs1;
    obs1.layer_id_ = 0;
    obs1.set_rect(GEORect(0, 0, 1400, 1400));
    inv.add_obs(obs1);
    design.add_cell(std::move(inv));

    DSCell filler;
    filler.name_ = "FILLER01";
    filler.class_ = "CORE_SPACER";
    filler.set_fake_cell();  // ⑲ fake cell（S5a 机制生成，此处结构就位）
    design.add_cell(std::move(filler));
    design.fake_cell_ids_.push_back(1);  // ⑳ fake 单独集合（P4 保留索引）

    DSViaCell via;
    via.name_ = "VIA1";
    via.bottom_layer_id_ = 0;
    via.top_layer_id_ = 2;
    via.add_cut_rect(GEORect(0, 0, 200, 200));
    via.add_bottom_enclosure(GEORect(-40, -40, 240, 240));
    design.add_via_cell(std::move(via));

    // ㉙：block = DSCell（block_cell 位 + bbox/polygon/def_path 等 block
    // 场景字段；port = cell.pins_ 的 port 位 DSPin）
    DSCell blk;
    blk.name_ = "top_block";
    blk.class_ = "BLOCK";
    blk.set_block_cell();
    blk.set_bbox(GEORect(-190000, -120000, 190360, 350000));
    blk.origin_x_ = 190000;
    blk.origin_y_ = 120000;
    blk.def_path_ = "/work/top.def";
    blk.def_units_per_micron_ = 1000;
    DSPin p0;
    p0.set_port();
    p0.placement_status_ = static_cast<uint8_t>(DSPinPlacementStatus::FIXED);
    blk.add_pin(std::move(p0));
    design.add_cell(std::move(blk));

    // namemap：cell/via 经 add_* 已注册；pin 组合键手动注册（D1 平
    // 铺 id）+ R4 回填 cell.pins_ 对应 DSPin 的 pin_id_
    design.register_pin("INV_X1", "A", 0);
    design.register_pin("INV_X1", "ZN", 1);
    design.register_pin("top_block", "PIN_A", 2);
    design.cells_[0].pins_[0].set_pin_id(0);
    design.cells_[0].pins_[1].set_pin_id(1);
    design.cells_[2].pins_[0].set_pin_id(2);
    design.lib_link_[0] = "INV_X1_lib";

    return design;
}

TEST(DSStackTest, SerializeRoundTripAndFindLayer) {
    DSStack stack = make_stack();

    CMString blob;
    FLY_ENCODE(stack, blob);
    DSStack back;
    FLY_DECODE(blob, DSStack, back);

    EXPECT_EQ(back.get_dbu_basis(), "test_dbu");
    EXPECT_EQ(back.get_dbu_per_micron(), 2000);
    EXPECT_EQ(back.get_manufacturing_grid(), 5);
    ASSERT_EQ(back.layer_count(), 3u);

    // 层序即索引序（自底向上）
    EXPECT_EQ(back.layer_at(0).get_name(), "M1");
    EXPECT_EQ(back.layer_at(0).get_type(),
              static_cast<uint8_t>(DSLayerType::ROUTING));
    EXPECT_EQ(back.layer_at(0).get_direction(),
              static_cast<uint8_t>(DSDirection::HORIZONTAL));
    EXPECT_EQ(back.layer_at(0).get_default_width(), 140);
    ASSERT_EQ(back.layer_at(0).get_spacing().size(), 2u);
    EXPECT_EQ(back.layer_at(0).get_spacing()[1], 160);
    EXPECT_EQ(back.layer_at(0).get_min_area(), 100000);
    EXPECT_EQ(back.layer_at(1).get_name(), "V1");
    EXPECT_EQ(back.layer_at(1).get_type(),
              static_cast<uint8_t>(DSLayerType::CUT));
    EXPECT_EQ(back.layer_at(2).get_name(), "M2");
    EXPECT_EQ(back.layer_at(2).get_direction(),
              static_cast<uint8_t>(DSDirection::VERTICAL));

    // find_layer 往返后可用（R7 ㊸：层名索引 = 序列化 DSLayerNameHasher，
    // 不再惰性重建）
    EXPECT_EQ(back.find_layer("V1"), 1u);
    EXPECT_EQ(back.find_layer("M2"), 2u);
    EXPECT_EQ(back.find_layer("NOT_EXIST"), DSStack::kNoLayer);

    // find_or_add_layer：重名保留首份返回既有下标
    DSLayer dup;
    dup.name_ = "M1";
    dup.default_width_ = 999;
    EXPECT_EQ(back.find_or_add_layer(std::move(dup)), 0u);
    EXPECT_EQ(back.layer_count(), 3u);
    EXPECT_EQ(back.layer_at(0).get_default_width(), 140);
}

TEST(DSStackTest, LayerIdAssignedAndKeptThroughRoundTrip) {
    // R2：层 id 由 add_layer 分配回填，随序列化持久化——下标巧合一致
    // 但不再作为约定，按 id 读取走 layer_by_id
    DSStack stack = make_stack();
    ASSERT_EQ(stack.layer_count(), 3u);
    EXPECT_EQ(stack.layer_at(0).get_id(), 0u);
    EXPECT_EQ(stack.layer_by_id(1).get_id(), 1u);
    EXPECT_EQ(stack.layer_by_id(2).get_id(), 2u);
    // layer_by_id 与 layer_at 同语义（旧名保留）
    EXPECT_EQ(&stack.layer_by_id(2), &stack.layer_at(2));

    CMString blob;
    FLY_ENCODE(stack, blob);
    DSStack back;
    FLY_DECODE(blob, DSStack, back);

    // round-trip 后 id 保持（显式持久化，不依赖下标隐式约定）
    EXPECT_EQ(back.layer_by_id(0).get_name(), "M1");
    EXPECT_EQ(back.layer_by_id(1).get_name(), "V1");
    EXPECT_EQ(back.layer_by_id(2).get_name(), "M2");
    EXPECT_EQ(back.layer_by_id(0).get_id(), 0u);
    EXPECT_EQ(back.layer_by_id(1).get_id(), 1u);
    EXPECT_EQ(back.layer_by_id(2).get_id(), 2u);

    // R7 ㊸：序列化层名 hasher 与 id 持久化互不影响
    EXPECT_EQ(back.find_layer("V1"), 1u);
    EXPECT_EQ(back.find_layer("NOT_EXIST"), DSStack::kNoLayer);
}

TEST(DSStackTest, LayerIdWithDuplicateNames) {
    // 重名场景：两条同名层都入层表、各得连续 id；name 索引保留首个
    // （find_layer 语义不变）
    DSStack stack;
    DSLayer first;
    first.name_ = "M1";
    DSLayer second;
    second.name_ = "M1";
    EXPECT_EQ(stack.add_layer(std::move(first)), 0u);
    EXPECT_EQ(stack.add_layer(std::move(second)), 1u);
    ASSERT_EQ(stack.layer_count(), 2u);
    EXPECT_EQ(stack.layer_by_id(0).get_id(), 0u);
    EXPECT_EQ(stack.layer_by_id(1).get_id(), 1u);
    EXPECT_EQ(stack.find_layer("M1"), 0u);  // 重名保留首个
}

TEST(DSDesignTest, SerializeRoundTripAllFields) {
    DSDesign design = make_design();

    CMString blob;
    FLY_ENCODE(design, blob);
    DSDesign back;
    FLY_DECODE(blob, DSDesign, back);

    // cell 字段全量（含简化 pin 与 obs 几何；㉞ bbox + 派生尺寸）
    ASSERT_EQ(back.cells_.size(), 3u);
    const DSCell& inv = back.cells_[0];
    EXPECT_EQ(inv.get_name(), "INV_X1");
    EXPECT_EQ(inv.width(), 1400);
    EXPECT_EQ(inv.height(), 1400);
    EXPECT_EQ(inv.get_bbox().get_x_high(), 1400);
    EXPECT_EQ(inv.get_class(), "CORE");
    EXPECT_EQ(inv.get_site(), "site1");
    EXPECT_FALSE(inv.is_fake_cell());
    EXPECT_TRUE(inv.is_lef_cell());
    EXPECT_TRUE(inv.is_macro_cell());
    EXPECT_FALSE(inv.is_block_cell());
    ASSERT_EQ(inv.pin_count(), 2u);
    // R7 ㊱：DSPin 无 name——pin 名经 pin hasher 组合键反查
    EXPECT_EQ(back.pin_name_of(inv.pin_at(0).get_pin_id()), "A");
    EXPECT_EQ(inv.pin_at(0).get_type(),
              static_cast<uint8_t>(DSPinType::SIGNAL));
    EXPECT_EQ(back.pin_name_of(inv.pin_at(1).get_pin_id()), "ZN");
    EXPECT_EQ(inv.pin_at(1).get_direction(),
              static_cast<uint8_t>(DSPinDirection::OUTPUT));
    // R4：全局 pin id 与放置状态随序列化保留（placement_status 非 port
    // 场景默认 NONE）
    EXPECT_EQ(inv.pin_at(0).get_pin_id(), 0u);
    EXPECT_EQ(inv.pin_at(1).get_pin_id(), 1u);
    EXPECT_EQ(inv.pin_at(0).get_placement_status(),
              static_cast<uint8_t>(DSPinPlacementStatus::NONE));
    ASSERT_EQ(inv.obs_count(), 1u);
    EXPECT_EQ(inv.obs_at(0).get_layer_id(), 0u);
    EXPECT_EQ(inv.obs_at(0).get_rect().get_x_high(), 1400);

    // via cell
    ASSERT_EQ(back.via_cells_.size(), 1u);
    const DSViaCell& via = back.via_cells_[0];
    EXPECT_EQ(via.get_name(), "VIA1");
    EXPECT_EQ(via.get_bottom_layer_id(), 0u);
    EXPECT_EQ(via.get_top_layer_id(), 2u);
    ASSERT_EQ(via.cut_rect_count(), 1u);
    EXPECT_EQ(via.cut_rect_at(0).get_x_high(), 200);
    ASSERT_EQ(via.bottom_enclosure_count(), 1u);
    EXPECT_EQ(via.bottom_enclosure_at(0).get_x_low(), -40);
    EXPECT_EQ(via.top_enclosure_count(), 0u);

    // block cell（㉙：DSCell 承载，block_cell 位 + bbox/origin/场景字段）
    // + port pin（port 位 + placement_status，R4/R5）
    ASSERT_GE(back.cells_.size(), 3u);
    const DSCell& blk = back.cells_[2];
    EXPECT_EQ(blk.get_name(), "top_block");
    EXPECT_EQ(blk.get_class(), "BLOCK");
    EXPECT_TRUE(blk.is_block_cell());
    EXPECT_FALSE(blk.is_lef_cell());
    EXPECT_EQ(blk.get_bbox().get_x_low(), -190000);
    EXPECT_EQ(blk.get_bbox().get_x_high(), 190360);
    EXPECT_EQ(blk.width(), 380360);
    EXPECT_EQ(blk.height(), 470000);
    EXPECT_EQ(blk.get_origin_x(), 190000);  // −diearea 左下角（P7）
    EXPECT_EQ(blk.get_origin_y(), 120000);
    EXPECT_EQ(blk.get_def_path(), "/work/top.def");
    EXPECT_EQ(blk.get_def_units_per_micron(), 1000);
    ASSERT_EQ(blk.pin_count(), 1u);
    const DSPin& port = blk.pin_at(0);
    EXPECT_EQ(back.pin_name_of(port.get_pin_id()), "PIN_A");
    EXPECT_TRUE(port.is_port());
    EXPECT_EQ(port.get_placement_status(),
              static_cast<uint8_t>(DSPinPlacementStatus::FIXED));
    EXPECT_EQ(port.get_pin_id(), 2u);

    // lib_link
    ASSERT_EQ(back.lib_link_.size(), 1u);
    EXPECT_EQ(back.lib_link_.at(0), "INV_X1_lib");
}

TEST(DSDesignTest, InjectedRuntimeFieldsNotSerialized) {
    // ⑱：注入 pin_tables_/pin_geometry_ 后 write→load，新对象两字段为空
    DSDesign design = make_design();
    design.set_pin_tables(std::make_shared<DSPinTables>());
    design.get_pin_tables()->add_timing_tables(0, {make_table("cell_rise", 1.0, 2.0)});
    design.set_pin_geometry(std::make_shared<DSPinGeometry>());
    DSShapeRef g;
    g.layer_id_ = 3;
    design.get_pin_geometry()->add_geometry(0, std::move(g));

    CMString blob;
    FLY_ENCODE(design, blob);
    DSDesign back;
    FLY_DECODE(blob, DSDesign, back);

    EXPECT_EQ(back.get_pin_tables(), nullptr);
    EXPECT_EQ(back.get_pin_geometry(), nullptr);

    // 注入字段不改变序列化面：往返前后 blob 尺寸一致（同一对象注入前后
    // 序列化字节完全相同，即注入不落库）
    DSDesign clean = make_design();
    CMString clean_blob;
    FLY_ENCODE(clean, clean_blob);
    EXPECT_EQ(blob.size(), clean_blob.size());
}

TEST(DSDesignTest, GetCellInjectionSharesPointers) {
    // ⑰：get_cell 注入的是指针共享非拷贝——cell 侧与容器侧 CMSharedPtr
    // 指向同一对象，经 cell 引用写入对容器立即可见
    DSDesign design = make_design();
    design.set_pin_tables(std::make_shared<DSPinTables>());
    design.set_pin_geometry(std::make_shared<DSPinGeometry>());

    const DSCell& inv = design.get_cell(0);
    ASSERT_TRUE(inv.get_pin_tables() != nullptr);
    EXPECT_TRUE(inv.get_pin_tables() == design.get_pin_tables());
    EXPECT_TRUE(inv.get_pin_geometry() == design.get_pin_geometry());

    // 经 cell 引用写入 → 容器侧可见（零拷贝）；R4 表按全局 pin id 落位
    inv.get_pin_tables()->add_internal_power_tables(
        0, {make_table("rise_power", 3.0, 4.0)});
    EXPECT_TRUE(design.get_pin_tables()->pin_has_tables(0));
    ASSERT_NE(design.get_pin_tables()->internal_power_tables_of(0), nullptr);
    EXPECT_EQ(design.get_pin_tables()->internal_power_tables_of(0)->size(), 1u);

    // 容器字段为空时注入置空（语义：该 design 无表/几何数据）
    DSDesign empty = make_design();
    EXPECT_TRUE(empty.get_cell(0).get_pin_tables() == nullptr);
}

TEST(DSDesignTest, FakeCellStructureInPlace) {
    // ⑲/⑳：is_fake_cell() flags 位与 fake_cell_ids_ 结构就位并随序列化保留
    DSDesign design = make_design();
    ASSERT_EQ(design.fake_cell_ids_.size(), 1u);
    EXPECT_TRUE(design.cells_[design.fake_cell_ids_[0]].is_fake_cell());
    EXPECT_FALSE(design.cells_[0].is_fake_cell());

    CMString blob;
    FLY_ENCODE(design, blob);
    DSDesign back;
    FLY_DECODE(blob, DSDesign, back);

    ASSERT_EQ(back.fake_cell_ids_.size(), 1u);
    EXPECT_EQ(back.fake_cell_ids_[0], 1u);
    EXPECT_TRUE(back.cells_[1].is_fake_cell());
    // fake cell 无 pin（⑲：fake 不在 pins_，无 pin）
    EXPECT_EQ(back.cells_[1].pin_count(), 0u);
}

TEST(DSDesignTest, NameMapBidirectionalConsistency) {
    // ②：name→id map 与 id→name vector 双向一致，且随序列化保留
    DSDesign design = make_design();

    CMString blob;
    FLY_ENCODE(design, blob);
    DSDesign back;
    FLY_DECODE(blob, DSDesign, back);

    // cell：双向闭环 find_cell ↔ cell_names_（hasher 双向）
    const DSCell* inv = back.find_cell("INV_X1");
    ASSERT_NE(inv, nullptr);
    const uint32_t inv_id = back.cell_names_.get_id("INV_X1");
    EXPECT_EQ(back.cell_names_.get_name(inv_id), "INV_X1");
    EXPECT_EQ(&back.cells_[inv_id], inv);
    EXPECT_EQ(back.find_cell("FILLER01"), &back.cells_[1]);
    EXPECT_EQ(back.find_cell("NOT_EXIST"), nullptr);

    // via cell / block cell 同构（㉙：block 走 cell namemap + is_block_cell）
    const DSViaCell* via = back.find_via_cell("VIA1");
    ASSERT_NE(via, nullptr);
    EXPECT_EQ(via->get_top_layer_id(), 2u);
    EXPECT_EQ(back.find_via_cell("NOPE"), nullptr);
    const DSCell* blk = back.find_cell("top_block");
    ASSERT_NE(blk, nullptr);
    EXPECT_TRUE(blk->is_block_cell());
    EXPECT_EQ(blk->get_def_units_per_micron(), 1000);
    EXPECT_EQ(back.find_cell("nope"), nullptr);

    // pin 组合键（cell_name/pin_name；block port pin 同一 pin id 空间）
    // 经 pin hasher 双向底座（R7 ㊲：assign 指定 id 双写 + 空洞容忍）
    EXPECT_EQ(back.pin_names_.get_id("INV_X1/A"), 0u);
    EXPECT_EQ(back.pin_names_.get_id("INV_X1/ZN"), 1u);
    EXPECT_EQ(back.pin_names_.get_id("top_block/PIN_A"), 2u);
    EXPECT_EQ(back.pin_names_.get_name(0), "INV_X1/A");
    EXPECT_EQ(back.pin_names_.get_name(1), "INV_X1/ZN");
    EXPECT_EQ(back.pin_names_.get_name(2), "top_block/PIN_A");
    EXPECT_EQ(back.pin_name_of(2), "PIN_A");  // 名段反查（㊱ 查名功能）
    EXPECT_EQ(back.pin_name_of(DSPinNameHasher::kInvalidId), "");
}

TEST(DSPinTablesTest, SerializeRoundTripAndQuery) {
    // R4：表按全局 pin id 组织——internal_power_tables_[pin_id]/
    // timing_tables_[pin_id] 条目仅含该 pin 自己的表
    DSPinTables tables;
    tables.add_internal_power_tables(
        0, {make_table("rise_power", 1.0, 2.0), make_table("fall_power", 3.0, 4.0)});
    tables.add_timing_tables(0, {make_table("cell_rise", 5.0, 6.0)});
    tables.add_timing_tables(7, {make_table("cell_rise", 7.0, 8.0)});

    EXPECT_TRUE(tables.pin_has_tables(0));
    EXPECT_TRUE(tables.pin_has_tables(7));
    EXPECT_FALSE(tables.pin_has_tables(3));

    CMString blob;
    FLY_ENCODE(tables, blob);
    DSPinTables back;
    FLY_DECODE(blob, DSPinTables, back);

    EXPECT_TRUE(back.pin_has_tables(0));
    EXPECT_TRUE(back.pin_has_tables(7));
    EXPECT_FALSE(back.pin_has_tables(3));

    const CMVector<CMLookupTable>* ip = back.internal_power_tables_of(0);
    ASSERT_NE(ip, nullptr);
    ASSERT_EQ(ip->size(), 2u);
    EXPECT_EQ((*ip)[0].name_, "rise_power");
    EXPECT_DOUBLE_EQ((*ip)[0].values_[1], 2.0);
    EXPECT_EQ((*ip)[1].name_, "fall_power");

    const CMVector<CMLookupTable>* tt7 = back.timing_tables_of(7);
    ASSERT_NE(tt7, nullptr);
    ASSERT_EQ(tt7->size(), 1u);
    EXPECT_DOUBLE_EQ((*tt7)[0].values_[0], 7.0);
    EXPECT_EQ(back.timing_tables_of(3), nullptr);
}

TEST(DSPinGeometryTest, SerializeRoundTripAndQuery) {
    // R4：几何按全局 pin id 组织——pin_geometry_[pin_id] = 该 pin 的
    // DSShapeRef 集
    DSPinGeometry geos;
    DSShapeRef g1;
    g1.layer_id_ = 0;
    g1.set_rect(GEORect(0, 0, 100, 200));
    DSShapeRef g2;
    g2.layer_id_ = 2;
    g2.set_rect(GEORect(50, 60, 70, 80));
    geos.add_geometries(5, {g1, g2});

    EXPECT_TRUE(geos.pin_has_geometry(5));
    EXPECT_FALSE(geos.pin_has_geometry(6));

    CMString blob;
    FLY_ENCODE(geos, blob);
    DSPinGeometry back;
    FLY_DECODE(blob, DSPinGeometry, back);

    EXPECT_TRUE(back.pin_has_geometry(5));
    const CMVector<DSShapeRef>* vec = back.geometry_of(5);
    ASSERT_NE(vec, nullptr);
    ASSERT_EQ(vec->size(), 2u);
    EXPECT_EQ((*vec)[0].get_layer_id(), 0u);
    EXPECT_EQ((*vec)[0].get_rect().get_y_high(), 200);
    EXPECT_EQ((*vec)[1].get_layer_id(), 2u);
    EXPECT_EQ((*vec)[1].get_rect().get_x_low(), 50);
    EXPECT_EQ(back.geometry_of(6), nullptr);
}

TEST(DSPinTest, NoNameMemberAndPinIdStatusRoundTrip) {
    // R4：DSPin 全局 pin_id_（D1 平铺）与 placement_status_（P3：仅 port
    // 场景有效）随序列化保留；非 port pin 默认 NONE。
    // R7 ㊱：DSPin 无 name 成员（类型层断言见文件尾 NameLayering 断言）
    DSPin plain;
    EXPECT_EQ(plain.get_pin_id(), 0u);
    EXPECT_EQ(plain.get_placement_status(),
              static_cast<uint8_t>(DSPinPlacementStatus::NONE));

    DSPin port_pin;
    port_pin.set_pin_id(9u);
    port_pin.set_placement_status(
        static_cast<uint8_t>(DSPinPlacementStatus::FIXED));

    CMString blob;
    FLY_ENCODE(port_pin, blob);
    DSPin back;
    FLY_DECODE(blob, DSPin, back);

    EXPECT_EQ(back.get_pin_id(), 9u);
    EXPECT_EQ(back.get_placement_status(),
              static_cast<uint8_t>(DSPinPlacementStatus::FIXED));
}

TEST(DSDesignTest, CellPinGeometriesAggregatesByPinId) {
    // R4：cell 维度便利聚合——cell 全部 pin（按 pins_ 序）的几何拼接；
    // pin 几何容器按全局 pin id 组织
    DSDesign design = make_design();
    design.set_pin_geometry(std::make_shared<DSPinGeometry>());
    DSShapeRef ga;  // pin id 0（INV_X1/A）
    ga.layer_id_ = 0;
    ga.set_rect(GEORect(0, 0, 100, 100));
    DSShapeRef gb;  // pin id 1（INV_X1/ZN）
    gb.layer_id_ = 2;
    gb.set_rect(GEORect(200, 200, 300, 300));
    design.get_pin_geometry()->add_geometry(0, std::move(ga));
    design.get_pin_geometry()->add_geometry(1, std::move(gb));

    const CMVector<DSShapeRef> agg = design.cell_pin_geometries(0);
    ASSERT_EQ(agg.size(), 2u);
    EXPECT_EQ(agg[0].get_layer_id(), 0u);  // A 先（pins_ 序）
    EXPECT_EQ(agg[1].get_layer_id(), 2u);  // ZN 后

    // 无几何数据（容器空）与无 pin 的 cell 均返回空集
    DSDesign bare = make_design();
    EXPECT_TRUE(bare.cell_pin_geometries(0).empty());
    EXPECT_TRUE(bare.cell_pin_geometries(1).empty());  // fake cell 无 pin
}

TEST(DSCellTest, FlagsIndependentAndCoexist) {
    // ㉗/㉙：CM_FLAGS 位独立可共存（is/set/reset 互不干扰），随序列化保留
    DSCell cell;
    cell.name_ = "blk";
    EXPECT_FALSE(cell.is_block_cell());
    EXPECT_FALSE(cell.is_polygon());
    cell.set_block_cell();
    cell.set_polygon();
    cell.set_lib_cell();
    EXPECT_TRUE(cell.is_block_cell());
    EXPECT_TRUE(cell.is_polygon());
    EXPECT_TRUE(cell.is_lib_cell());
    EXPECT_FALSE(cell.is_fake_cell());  // 未置位不受影响
    cell.reset_polygon();
    EXPECT_FALSE(cell.is_polygon());
    EXPECT_TRUE(cell.is_block_cell());  // 其余位不变
    cell.reset_flags();
    EXPECT_FALSE(cell.is_block_cell());
    EXPECT_FALSE(cell.is_lib_cell());

    cell.set_block_cell();
    cell.set_polygon();
    CMString blob;
    FLY_ENCODE(cell, blob);
    DSCell back;
    FLY_DECODE(blob, DSCell, back);
    EXPECT_TRUE(back.is_block_cell());
    EXPECT_TRUE(back.is_polygon());
    EXPECT_FALSE(back.is_lib_cell());
}

TEST(DSCellTest, BboxPolygonDualStorageAndDerivedSize) {
    // ㉞：bbox 恒存（含左下角坐标，可反推宽高）；polygon 双存仅多点
    // DIEAREA 时非空 + is_polygon 置位
    DSCell cell;
    cell.set_bbox(GEORect(-2500, -2500, 2500, 2500));
    EXPECT_EQ(cell.width(), 5000);
    EXPECT_EQ(cell.height(), 5000);
    EXPECT_EQ(cell.get_bbox().get_x_low(), -2500);
    EXPECT_EQ(cell.get_polygon().points_.size(), 0u);  // 矩形场景 polygon 空
    EXPECT_FALSE(cell.is_polygon());

    GEOPolygon poly;
    poly.get_ref_points() = {GEOPoint(-2500, -2500),
                             GEOPoint(-2500, 2500),
                             GEOPoint(2500, 2500),
                             GEOPoint(2500, -2500)};
    cell.assign_polygon(std::move(poly));
    cell.set_polygon();
    ASSERT_EQ(cell.get_polygon().points_.size(), 4u);
    EXPECT_TRUE(cell.is_polygon());

    CMString blob;
    FLY_ENCODE(cell, blob);
    DSCell back;
    FLY_DECODE(blob, DSCell, back);
    EXPECT_EQ(back.get_bbox().get_x_high(), 2500);
    EXPECT_EQ(back.width(), 5000);
    ASSERT_EQ(back.get_polygon().points_.size(), 4u);
    EXPECT_EQ(back.get_polygon().points_[2].get_x(), 2500);
    EXPECT_TRUE(back.is_polygon());
}

// —— DSShapeRef（带 layer id 的几何引用，R1 随 geometry 模块化迁入
//    design；接口与序列化用例由原 geometry_types_test 的 CMGeometryRef
//    用例承接迁移）——
TEST(DSShapeRefTest, AccessorsAndRoundTrip) {
    DSShapeRef ref;
    ref.set_layer_id(7u);
    ref.set_rect(GEORect(0, 0, 100, 200));

    EXPECT_EQ(ref.get_layer_id(), 7u);
    EXPECT_EQ(ref.get_rect().width(), 100);
    EXPECT_EQ(ref.get_rect().height(), 200);
    EXPECT_TRUE(ref.get_cref_rect().contains(GEOPoint(50, 100)));
    EXPECT_FALSE(ref.get_cref_rect().contains(GEOPoint(100, 200)));

    CMString blob;
    FLY_ENCODE(ref, blob);
    DSShapeRef back;
    FLY_DECODE(blob, DSShapeRef, back);

    EXPECT_EQ(back.get_layer_id(), 7u);
    EXPECT_EQ(back.get_rect().get_x_low(), 0);
    EXPECT_EQ(back.get_rect().get_y_high(), 200);
}

// —— R7 ㊱ name 分层：DSPin/DSInstance 无 name 成员的类型层断言 ———
// （SFINAE 探测成员存在性；编译期即锁死「name 不回流实体」的终局）

template <typename T, typename = void>
struct has_name_member : std::false_type {};
template <typename T>
struct has_name_member<T, std::void_t<decltype(&T::name_)>>
    : std::true_type {};

static_assert(!has_name_member<DSPin>::value,
              "DSPin must not store name (R7 ㊱ name 分层存储)");
static_assert(!has_name_member<DSInstance>::value,
              "DSInstance must not store name (R7 ㊱ name 分层存储)");
static_assert(has_name_member<DSCell>::value,
              "DSCell keeps name (cell 名是权威存储)");
static_assert(has_name_member<DSViaCell>::value,
              "DSViaCell keeps name (via cell 名是权威存储)");

TEST(DSNameLayeringTest, InstanceNameLookupViaHasher) {
    // ㊱ 功能断言：DSInstance 无 name，实例名经 DSInstanceNameHasher
    // 查回（DSBlockBuildData::find_instance_by_name / add_instance 登记）
    DSBlockBuildData block;
    block.init_placeholder("blk_a", DSCellNameHasher::kInvalidId);

    DSInstance inst;
    inst.set_cell_id(3u);
    const uint64_t id = block.add_instance(std::move(inst), "u1");

    const DSInstance* found = block.find_instance_by_name("u1");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, block.find_instance(id));
    EXPECT_EQ(block.find_instance_by_name("ghost"), nullptr);

    CMString blob;
    FLY_ENCODE(block, blob);
    DSBlockBuildData back;
    FLY_DECODE(blob, DSBlockBuildData, back);
    // ㊵②：实例表（instances_）随 DSBlock_<i> 序列化保留，名字 hasher
    // 不在其中——读回后 find_instance 可用、find_instance_by_name 不可用
    //（名字经 DSBlockNames_<i> 伴生对象 / mapper）
    ASSERT_EQ(back.instance_total(), 2u);  // local 0 占位 + u1
    EXPECT_NE(back.find_instance(id), nullptr);
    EXPECT_EQ(back.find_instance(id)->get_cell_id(), 3u);
    EXPECT_EQ(back.find_instance_by_name("u1"), nullptr);

    // ㊵②：读伴生对象 attach 后查名恢复（CMSharedPtr 共享注入）
    DSBlockNames names_from;
    names_from.instance_names_->assign("u1", id);
    back.set_instance_names(names_from.instance_names_);
    EXPECT_EQ(back.instance_names_->get_name(id), "u1");
}

TEST(DSNameLayeringTest, PinNameLookupViaDesignHasher) {
    // ㊱ 功能断言：DSPin 无 name，pin 名经 DSDesign pin hasher 反查
    DSDesign design = make_design();
    EXPECT_EQ(design.pin_name_of(0), "A");
    EXPECT_EQ(design.pin_name_of(2), "PIN_A");
    // 未登记/越界/哨兵 → 空串（不透出哨兵值）
    EXPECT_EQ(design.pin_name_of(99), "");
    EXPECT_EQ(design.pin_name_of(DSPinNameHasher::kInvalidId), "");
}

TEST(DSNameLayeringTest, FinalizeNamesForSaveSealsBothHashers) {
    // R8d（裁定 55）：finalize_names_for_save——alpha 键 lcp_name_arena
    // 的封口入口。64 位组 instance/net 两 hasher 同步封口；32 位组
    // （DSDesign cell/pin hasher）不在封口面（本类只持 64 位组）。
    DSBlockBuildData block;
    block.init_placeholder("blk_a", DSCellNameHasher::kInvalidId);
    DSInstance inst;
    inst.set_cell_id(3u);
    block.add_instance(std::move(inst), "u1");
    block.register_net("n1");
    ASSERT_NE(block.instance_names_, nullptr);
    ASSERT_NE(block.net_names_, nullptr);

    // false 恒无操作（形态一默认路径零变化）
    block.finalize_names_for_save(false);
    EXPECT_FALSE(block.instance_names_->is_lcp_form());
    EXPECT_FALSE(block.net_names_->is_lcp_form());

    block.finalize_names_for_save(true);
    EXPECT_TRUE(block.instance_names_->is_lcp_form());
    EXPECT_TRUE(block.net_names_->is_lcp_form());
    // 封口后查询面可用、构建期接口拒绝
    EXPECT_EQ(block.instance_names_->get_name(1), "u1");
    EXPECT_EQ(block.net_names_->get_id("n1"), 1u);
    EXPECT_THROW(block.net_names_->emplace("n2"), std::logic_error);

    // 伴生对象（与运行时 hasher 同一实例，extract_names 同构）落盘
    // round-trip：标记位自识别读回封口形态
    DSBlockNames names;
    names.block_name_ = block.block_name_;
    names.instance_names_ = block.instance_names_;
    names.net_names_ = block.net_names_;
    CMString blob;
    FLY_ENCODE(names, blob);
    DSBlockNames back;
    FLY_DECODE(blob, DSBlockNames, back);
    EXPECT_TRUE(back.instance_names_->is_lcp_form());
    EXPECT_TRUE(back.net_names_->is_lcp_form());
    EXPECT_EQ(back.instance_names_->get_name(1), "u1");
    EXPECT_EQ(back.net_names_->get_name(1), "n1");
}

}  // namespace
