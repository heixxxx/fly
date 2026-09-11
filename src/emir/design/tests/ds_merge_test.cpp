// design db 汇总与 merge 单测（T6）：S2 汇总（ds_merge_cell_lef：cell id
// 统一分配、pin namemap 重挂 + pin_id 回填、via 合入、pin 几何按新全局
// pin id 重挂、抛弃 pin 的几何随之丢弃）、S4/S4b 汇总（ds_merge_def_header
// 新形态：block cell 进 cell namemap、port pin id 平铺分配回填 + port
// 几何重挂、via 权威表）、S3（merge_lib：字段填充/lib_link/逐 pin 表
// 提取/不匹配名单计数）。
#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/lib/cpp/lib_types.h>

#include <gtest/gtest.h>

namespace {

using namespace fly;

// ㉙：block = DSCell（block_cell 位 + bbox/polygon/origin 场景字段），
// port = pins_ 的 port 位 DSPin（几何由调用方经独立 DSPinGeometry 承载，
// 键 = 局部 pin 下标）
DSCell make_block_cell(const char* design_name) {
    DSCell blk;
    blk.name_ = design_name;
    blk.class_ = "BLOCK";
    blk.set_block_cell();
    blk.set_bbox(GEORect(0, 0, 2000, 1000));
    blk.origin_x_ = 0;
    blk.origin_y_ = 0;
    blk.def_path_ = "/work/x.def";
    blk.def_units_per_micron_ = 1000;
    DSPin p;  // R7 ㊱：DSPin 无 name（port 名经 ds_merge_def_header 的
              // port_names 参数进全局 pin hasher）
    p.set_port();
    p.direction_ = static_cast<uint8_t>(DSPinDirection::INPUT);
    blk.add_pin(std::move(p));
    return blk;
}

TEST(DsMergeCellLefTest, MergePartsRemapsIdsAndGeometry) {
    DSStack stack;  // merge 不用 stack
    // part A：2 cell（INV + FILLER fake）+ 1 via
    DSDesign part_a;
    DSCell inv;
    inv.name_ = "INV_X1";
    DSPin a;  // R7 ㊱：DSPin 无 name（pin 名经 register_pin 进 hasher）
    inv.add_pin(std::move(a));
    part_a.add_cell(std::move(inv));
    part_a.cells_[0].pins_[0].set_pin_id(0);  // 局部 pin id 回填（解析链路
    // 由适配层完成，此处对齐 make_design 先例——merge 按 pin_id_ 反查）
    DSCell filler;
    filler.name_ = "FILLER01";
    filler.set_fake_cell();
    part_a.add_cell(std::move(filler));
    part_a.fake_cell_ids_.push_back(1);
    part_a.register_pin("INV_X1", "A", 0);
    DSViaCell via;
    via.name_ = "VIA12";
    part_a.add_via_cell(std::move(via));
    DSPinGeometry geoms_a;
    DSShapeRef ga;
    ga.layer_id_ = 0;
    ga.set_rect(GEORect(0, 0, 10, 10));
    geoms_a.add_geometry(0, std::move(ga));  // INV_X1/A（局部 pin id 0）

    // part B：INV 重名（保留 part A 首份）+ BUF 新 cell + VIA12 重名
    DSDesign part_b;
    DSCell inv2;
    inv2.name_ = "INV_X1";
    part_b.add_cell(std::move(inv2));
    DSCell buf;
    buf.name_ = "BUF_X1";
    DSPin z;
    buf.add_pin(std::move(z));
    part_b.add_cell(std::move(buf));
    part_b.cells_[1].pins_[0].set_pin_id(1);  // BUF_X1/Z 局部 pin id
    part_b.register_pin("INV_X1", "A", 0);
    part_b.register_pin("BUF_X1", "Z", 1);
    DSViaCell via2;
    via2.name_ = "VIA12";
    part_b.add_via_cell(std::move(via2));
    DSPinGeometry geoms_b;
    DSShapeRef gb;  // BUF_X1/Z 的几何（局部 pin id 1，应重挂到全局 id 2）
    gb.layer_id_ = 2;
    gb.set_rect(GEORect(5, 5, 15, 15));
    geoms_b.add_geometry(1, std::move(gb));
    DSShapeRef gdup;  // 重名 INV_X1 pin 的几何（应随 cell 丢弃）
    gdup.layer_id_ = 0;
    gdup.set_rect(GEORect(99, 99, 100, 100));
    geoms_b.add_geometry(0, std::move(gdup));

    DSDesign dst;
    DSPinGeometry dst_geoms;
    const int conflicts_a = ds_merge_cell_lef(dst, part_a, dst_geoms, geoms_a);
    const int conflicts_b = ds_merge_cell_lef(dst, part_b, dst_geoms, geoms_b);

    // cell id 统一分配：A 两条（0/1）+ B 一条（2）；INV 重名抛弃
    EXPECT_EQ(conflicts_a, 0);
    EXPECT_EQ(conflicts_b, 2);  // INV_X1 + VIA12 两个重名
    ASSERT_EQ(dst.cells_.size(), 3u);
    EXPECT_EQ(dst.cells_[0].get_name(), "INV_X1");
    EXPECT_EQ(dst.cells_[2].get_name(), "BUF_X1");

    // pin namemap 重挂：单调分配（D1，容忍空洞）。基址 = A 合并后的
    // pin 数（1）；B 的局部 id 空间中 INV_X1/A=0 已被消费（cell 虽被
    // 抛弃、id 已分配）→ BUF_X1/Z = 1 + 1 = 2，全局 id 1 为被抛弃 pin
    // 的无害空洞
    EXPECT_EQ(dst.pin_names_.get_id("INV_X1/A"), 0u);
    EXPECT_EQ(dst.pin_names_.get_id("BUF_X1/Z"), 2u);

    // R4：重挂后全局 pin id 回填 cell.pins_（跨 cell 全局平铺一致）
    EXPECT_EQ(dst.cells_[0].pins_[0].get_pin_id(), 0u);  // INV_X1/A
    EXPECT_EQ(dst.cells_[2].pins_[0].get_pin_id(), 2u);  // BUF_X1/Z

    // fake cell ids 重挂
    ASSERT_EQ(dst.fake_cell_ids_.size(), 1u);
    EXPECT_EQ(dst.fake_cell_ids_[0], 1u);
    EXPECT_TRUE(dst.cells_[1].is_fake_cell());

    // via 合入：A 的 VIA12 保留，B 的重名抛弃
    ASSERT_EQ(dst.via_cells_.size(), 1u);
    EXPECT_EQ(dst.via_cells_[0].get_name(), "VIA12");

    // pin 几何按新分配的全局 pin id 重挂（R4：键 = 全局 pin id）；抛弃
    // pin 的几何丢弃
    ASSERT_NE(dst_geoms.geometry_of(0), nullptr);  // INV_X1/A（全局 pin id 0）
    EXPECT_EQ(dst_geoms.geometry_of(0)->size(), 1u);
    ASSERT_NE(dst_geoms.geometry_of(2), nullptr);  // BUF_X1/Z（全局 pin id 2）
    EXPECT_EQ(dst_geoms.geometry_of(2)->at(0).get_rect().get_x_low(), 5);
    EXPECT_EQ(dst_geoms.geometry_of(1), nullptr);  // 被抛弃 pin 无几何
}

TEST(DsMergeDefHeaderTest, BlocksEnterCellNameMapAndViaTable) {
    DSDesign dst;
    DSCell macro;
    macro.name_ = "INV_X1";
    macro.add_pin([] {
        DSPin p;
        return p;
    }());
    dst.add_cell(std::move(macro));
    dst.register_pin("INV_X1", "A", 0);

    CMVector<DSCell> block_cells;
    block_cells.push_back(make_block_cell("block_a"));
    block_cells.push_back(make_block_cell("block_a"));  // 重名（保留首份）

    // port 几何（键 = 局部 pin 下标）
    DSPinGeometry port_geoms;
    DSShapeRef pg;
    pg.layer_id_ = 0;
    pg.set_rect(GEORect(10, 20, 30, 40));
    port_geoms.add_geometry(0, std::move(pg));

    CMVector<DSViaCell> def_vias;
    DSViaCell v;
    v.name_ = "block_a::VIA12";  // ⑫ 前缀名
    def_vias.push_back(std::move(v));

    DSPinGeometry dst_geoms;
    const CMVector<CMString> port_names = {"P0"};  // 与 pins_ 下标对齐
    const int conflicts = ds_merge_def_header(dst, block_cells, port_names,
                                              dst_geoms, port_geoms,
                                              def_vias);

    EXPECT_EQ(conflicts, 1);  // block_a 第二次 = 重名

    // block cell 进 cell namemap（与 macro 同空间；㉙ 查找即 find_cell +
    // is_block_cell，无独立 blocks_ 表）
    ASSERT_NE(dst.find_cell("block_a"), nullptr);
    EXPECT_TRUE(dst.find_cell("block_a")->is_block_cell());
    EXPECT_EQ(dst.cells_.size(), 2u);  // INV_X1 + block_a
    EXPECT_EQ(dst.cells_[1].get_bbox().get_x_high(), 2000);
    EXPECT_EQ(dst.cells_[1].get_origin_y(), 0);
    EXPECT_EQ(dst.cells_[1].get_def_path(), "/work/x.def");

    // port pin id 平铺分配进 pin namemap + pin_id_ 回填（基址 = macro
    // pin 占用的 1）
    EXPECT_EQ(dst.pin_names_.get_id("block_a/P0"), 1u);
    EXPECT_EQ(dst.pin_names_.get_name(1), "block_a/P0");
    EXPECT_EQ(dst.cells_[1].pins_[0].get_pin_id(), 1u);

    // port 几何按全局 pin id 重挂
    ASSERT_NE(dst_geoms.geometry_of(1), nullptr);
    EXPECT_EQ(dst_geoms.geometry_of(1)->at(0).get_rect().get_x_low(), 10);

    // port pin 形态（㉙：port 位 DSPin）
    EXPECT_TRUE(dst.cells_[1].pins_[0].is_port());

    // via 权威表
    ASSERT_NE(dst.find_via_cell("block_a::VIA12"), nullptr);
    EXPECT_EQ(dst.via_cells_.size(), 1u);
}

TEST(DsMergeLibTest, MatchesFillFieldsExtractsTablesAndReportsMismatch) {
    // lef 侧全局容器：INV_X1（与 lib 完全匹配）+ SPIDEY（lef 有 lib 无）
    DSDesign dst;
    DSCell inv;
    inv.name_ = "INV_X1";
    DSPin a;
    inv.add_pin(std::move(a));
    DSPin zn;
    inv.add_pin(std::move(zn));
    dst.add_cell(std::move(inv));
    DSCell odd;
    odd.name_ = "LEF_ONLY";
    dst.add_cell(std::move(odd));
    // R4：merge_lib 逐 pin 落位按全局 pin id（namemap 查询），先注册
    dst.register_pin("INV_X1", "A", 0);
    dst.register_pin("INV_X1", "ZN", 1);

    // lib 侧：INV_X1（带表 + pin 集合缺 ZN → 0004）+ LIB_ONLY（0003）
    LIBLibrary lib;
    LIBCell linv;
    linv.name_ = "INV_X1";
    linv.library_name_ = "nangate45_typ";
    LIBPin lpin;
    lpin.name_ = "A";
    LIBInternalPower ip;
    ip.related_pin_ = "A";
    CMLookupTable t1;
    t1.name_ = "rise_power";
    t1.values_ = {1.0, 2.0};
    ip.tables_.push_back(t1);
    lpin.internal_powers_.push_back(ip);
    LIBTimingArc arc;
    arc.related_pin_ = "A";
    CMLookupTable t2;
    t2.name_ = "cell_rise";
    t2.values_ = {3.0, 4.0};
    arc.tables_.push_back(t2);
    lpin.timings_.push_back(arc);
    linv.pins_.push_back(lpin);  // lib 侧缺 ZN
    LIBCell llib_only;
    llib_only.name_ = "LIB_ONLY";
    lib.cells_.push_back(std::move(linv));
    lib.cells_.push_back(std::move(llib_only));

    const int matched = dst.merge_lib(lib);

    EXPECT_EQ(matched, 1);
    // 匹配 cell 填 lib 字段 + lib_link
    EXPECT_EQ(dst.cells_[0].get_library_name(), "nangate45_typ");
    EXPECT_EQ(dst.lib_link_.at(0), "INV_X1");
    // lef 有 lib 无 → 无 lib 字段
    EXPECT_EQ(dst.cells_[1].get_library_name(), "");

    // ⑰：pin 表提取（R4 逐 pin 落位——lib pin 名查 namemap 得全局 pin id，
    // pin id 0 = INV_X1/A 持 internal_power + timing 两类表）
    ASSERT_TRUE(dst.get_pin_tables() != nullptr);
    EXPECT_TRUE(dst.get_pin_tables()->pin_has_tables(0));
    // ㉗：S3 匹配 cell 置 lib_cell 位
    EXPECT_TRUE(dst.cells_[0].is_lib_cell());
    EXPECT_FALSE(dst.cells_[1].is_lib_cell());
    const CMVector<CMLookupTable>* ip_t =
        dst.get_pin_tables()->internal_power_tables_of(0);
    ASSERT_NE(ip_t, nullptr);
    ASSERT_EQ(ip_t->size(), 1u);
    EXPECT_EQ((*ip_t)[0].name_, "rise_power");
    const CMVector<CMLookupTable>* tm_t =
        dst.get_pin_tables()->timing_tables_of(0);
    ASSERT_NE(tm_t, nullptr);
    EXPECT_EQ((*tm_t)[0].name_, "cell_rise");
    // 无表 pin（ZN 无 lib 表 → 键 1 无条目）与 lef-only cell（id 1）无表
    EXPECT_FALSE(dst.get_pin_tables()->pin_has_tables(1));
}

}  // namespace
