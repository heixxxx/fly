// S5b 网内容责任链 + 批处理单测（裁定 ③④⑨⑩⑪⑫，方案
// design-db-phase2-plan.md §2.3 + design-db-plan.md S5b 节）：
//   1. 链骨架：顺序执行 + 错误标记即停（仿 S5a §2.1 形态）；
//   2. ConnectionParseNode：local net id 对齐（⑨ S5a namemap）+ 连接表
//      保留（S7 并查集输入）+ port 引用判别 + 未收录网兜底计数；
//   3. GeometryExpandNode：wire 段（layer id + 宽度回填缺省值 + 点列）/
//      rect 项 / via 命名引用解析（⑪ 权威表：plain → design:: 前缀回退，
//      ⑫）/ 未定义 via 跳过计数（兜底不 raise）/ VIADATA 阵列展开
//      （⑩ via instance 专用 id 空间从 1 起、无 name）；
//   4. DensityNode：金属（wire 段矩形 + rect）/ 通孔（via cut 图形）计数
//      进 DSDensityGrid 逐层分列通道（⑥ 分类分层保存）；
//   5. DSNetBuildData（⑬ 大体量独立对象）序列化往返；
//   6. 适配层 ds_parse_def_nets：真 DEF 全量网内容（SkipNetDetails 去除、
//      NETS/SPECIALNETS 内容回调、COMPONENTS 保持跳过）+ 分批落批
//      （批大小 2 强制多批，产物与单批一致）。
// 数据：data/nets_synth.def（wire/via/RECT/special net/⑫ 前缀 via/未定义
//       via 兜底）+ data/tech_synth.lef（层表 + plain 名 via）。
#include <emir/design/cpp/ds_def_adapter.h>
#include <emir/design/cpp/ds_lef_adapter.h>
#include <emir/design/cpp/ds_net_pipeline.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace {

namespace fs = std::filesystem;

using namespace fly;

fs::path test_data(const char* name) {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    const char* workspace = std::getenv("TEST_WORKSPACE");
    if (srcdir && workspace) {
        return fs::path(srcdir) / workspace / "src/emir/design/tests/data" /
               name;
    }
    return fs::path("data") / name;
}

// 标准环境：tech lef 层表（M1=0/VIA1=1/M2=2，M1 缺省宽 0.07µm×2000=140）
// + INV_X1 cell + tech VIA12（plain 名权威表条目）+ block 前缀 via。
struct TestEnv {
    DSStack stack;
    DSDesign design;

    TestEnv() {
        CMVector<DSViaCell> tech_vias;
        ds_parse_tech_lef(test_data("tech_synth.lef").string(), stack,
                          tech_vias);
        // tech via 并入权威表（S2/S4 汇总同型操作）
        for (auto& v : tech_vias) {
            design.add_via_cell(std::move(v));
        }
        // ⑫ 前缀 via（模拟本 DEF VIAS 段登记形态）：cut 层 VIA1(1)，
        // cut 矩形 ±80 DBU
        DSViaCell prefixed;
        prefixed.set_name("nets_blk::VIADEF1");
        prefixed.set_bottom_layer_id(0);
        prefixed.set_top_layer_id(2);
        prefixed.set_cut_layer_id(1);
        prefixed.add_cut_rect(GEORect(-80, -80, 80, 80));
        design.add_via_cell(std::move(prefixed));

        DSCell inv;
        inv.set_name("INV_X1");
        inv.set_lef_cell();
        inv.set_bbox(GEORect(0, 0, 1400, 1400));
        design.add_cell(std::move(inv));
    }

    // 挂好环境的 ctx（local 0 占位 + 两网名，仿 S5a 产物）
    DSBlockBuildData make_block_data() const {
        DSBlockBuildData block_data;
        block_data.init_placeholder("nets_blk", DSDesign::kInvalidId);
        block_data.register_net("n1");
        block_data.register_net("VDD");
        return block_data;
    }
};

DSNetContext make_ctx(const TestEnv& env, const DSBlockBuildData& block_data,
                      DSNetBuildData& net_data, const char* net_name) {
    DSNetContext ctx;
    ctx.net_name = net_name;
    ctx.stack = &env.stack;
    ctx.design = &env.design;
    ctx.block_data = &block_data;
    ctx.net_data = &net_data;
    ctx.design_name = "nets_blk";
    return ctx;
}

// ── 1. 链骨架：顺序执行 + 错误即停 ──────────────────────────────────

class NetOrderRecorderNode : public DSNetHandler {
public:
    explicit NetOrderRecorderNode(std::string tag, bool fail = false)
        : tag_(std::move(tag)), fail_(fail) {}
    const char* name() const override { return tag_.c_str(); }
    void handle(DSNetContext& ctx) override {
        order->push_back(tag_);
        if (fail_) ctx.error = true;
    }
    CMVector<std::string>* order = nullptr;

private:
    std::string tag_;
    bool fail_;
};

TEST(DSNetPipelineTest, RunsInOrderAndStopsOnError) {
    DSNetPipeline pipeline;
    CMVector<std::string> order;
    auto a = std::make_unique<NetOrderRecorderNode>("a", false);
    auto b = std::make_unique<NetOrderRecorderNode>("b", true);
    auto c = std::make_unique<NetOrderRecorderNode>("c", false);
    a->order = &order;
    b->order = &order;
    c->order = &order;
    pipeline.add(std::move(a));
    pipeline.add(std::move(b));
    pipeline.add(std::move(c));

    DSNetContext ctx;
    pipeline.run(ctx);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "a");
    EXPECT_EQ(order[1], "b");
    EXPECT_TRUE(ctx.error);
}

// ── 2. ConnectionParseNode ──────────────────────────────────────────

TEST(DSNetConnectionParseNodeTest, AlignsLocalIdAndKeepsTopology) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"u1", "A"});
    ctx.connections.push_back({"PIN", "PIN_A"});

    DSNetConnectionParseNode node;
    node.handle(ctx);

    // ⑨ local net id 沿用 S5a 网名扫描分配（n1 = 1）
    EXPECT_EQ(ctx.local_net_id, 1u);
    ASSERT_EQ(net_data.connections_.at(1).size(), 2u);
    EXPECT_EQ(net_data.connections_.at(1)[0].instance_name_, "u1");
    EXPECT_EQ(net_data.connections_.at(1)[0].pin_name_, "A");
    // block 级 port 引用（defi instance() = "PIN"）判别
    EXPECT_TRUE(net_data.connections_.at(1)[1].is_port_ref());
    EXPECT_EQ(net_data.stats_.net_count, 1u);
    EXPECT_EQ(net_data.stats_.connection_count, 2u);
}

TEST(DSNetConnectionParseNodeTest, UnknownNetCountedAndSkipped) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    // 网名不在 S5a namemap（防御兜底）：id = 0，后续节点跳过、计数不 crash
    DSNetContext ctx = make_ctx(env, block_data, net_data, "ghost_net");
    ctx.connections.push_back({"u1", "A"});
    ctx.wires.push_back({"M1", 0, {GEOPoint(0, 0)}});

    DSNetPipeline pipeline = ds_make_nets_pipeline();
    pipeline.run(ctx);

    EXPECT_EQ(ctx.local_net_id, 0u);
    EXPECT_FALSE(ctx.error);  // 兜底计数不拦截（dev-rules：仅两类可 raise）
    EXPECT_EQ(net_data.stats_.skipped_net_count, 1u);
    EXPECT_TRUE(net_data.connections_.empty());
    EXPECT_TRUE(net_data.wires_.empty());
}

// ── 3. GeometryExpandNode ───────────────────────────────────────────

TEST(DSNetGeometryExpandNodeTest, ExpandsWireWithDefaultWidthFallback) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    // 普通 net 无显式宽度（width_dbu = 0）→ 回填 M1 层缺省宽 140
    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.wires.push_back({"M1", 0, {GEOPoint(200, 200),
                                   GEOPoint(1000, 200)}});

    DSNetConnectionParseNode conn;
    DSNetGeometryExpandNode geo;
    conn.handle(ctx);
    geo.handle(ctx);

    ASSERT_EQ(net_data.wires_.at(1).size(), 1u);
    const DSNetWire& wire = net_data.wires_.at(1)[0];
    EXPECT_EQ(wire.layer_id_, 0u);  // M1
    EXPECT_EQ(wire.width_, 140);    // 0.07 µm × 2000 DBU/µm（tech 缺省宽）
    ASSERT_EQ(wire.points_.size(), 2u);
    EXPECT_EQ(wire.points_[0].get_x(), 200);
    EXPECT_EQ(wire.points_[1].get_y(), 200);
    EXPECT_EQ(net_data.stats_.wire_count, 1u);
}

TEST(DSNetGeometryExpandNodeTest, KeepsExplicitWidthAndRect) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    // special net 显式宽度保留；RECT 项展开
    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.is_special = true;
    ctx.wires.push_back({"M2", 400, {GEOPoint(0, 200)}});
    ctx.rects.push_back({"M2", GEORect(-560, -320, 4240, 320)});

    DSNetConnectionParseNode conn;
    DSNetGeometryExpandNode geo;
    conn.handle(ctx);
    geo.handle(ctx);

    ASSERT_EQ(net_data.wires_.at(2).size(), 1u);
    EXPECT_EQ(net_data.wires_.at(2)[0].layer_id_, 2u);  // M2
    EXPECT_EQ(net_data.wires_.at(2)[0].width_, 400);    // 显式宽度不回填
    ASSERT_EQ(net_data.rects_.at(2).size(), 1u);
    EXPECT_EQ(net_data.rects_.at(2)[0].layer_id_, 2u);
    EXPECT_EQ(net_data.rects_.at(2)[0].rect_.get_x_high(), 4240);
    EXPECT_EQ(net_data.stats_.wire_count, 1u);
    EXPECT_EQ(net_data.stats_.rect_count, 1u);
}

TEST(DSNetGeometryExpandNodeTest, ResolvesViaPlainThenPrefixed) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    // ⑪ 权威表引用解析：plain 名（tech lef）与 ⑫ design:: 前缀名（DEF VIAS）
    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.vias.push_back({"VIA12", 1000, 600, 1, 1, 0, 0});
    ctx.vias.push_back({"VIADEF1", 800, 1600, 1, 1, 0, 0});

    DSNetConnectionParseNode conn;
    DSNetGeometryExpandNode geo;
    conn.handle(ctx);
    geo.handle(ctx);

    // ⑩ via instance：专用 id 空间从 1 起、无 name，仅 via cell id + 位置
    ASSERT_EQ(net_data.via_instances_.size(), 2u);
    const DSViaInstance& v1 = net_data.via_instances_.at(1);
    EXPECT_EQ(v1.via_cell_id_, env.design.via_cell_names_.get_id("VIA12"));
    EXPECT_EQ(v1.pos_.get_x(), 1000);
    EXPECT_EQ(v1.pos_.get_y(), 600);
    const DSViaInstance& v2 = net_data.via_instances_.at(2);
    EXPECT_EQ(v2.via_cell_id_,
              env.design.via_cell_names_.get_id("nets_blk::VIADEF1"));
    EXPECT_EQ(v2.pos_.get_y(), 1600);
    // 网归属表
    ASSERT_EQ(net_data.net_via_ids_.at(1).size(), 2u);
    EXPECT_EQ(net_data.net_via_ids_.at(1)[0], 1u);
    EXPECT_EQ(net_data.stats_.via_instance_count, 2u);
    EXPECT_EQ(net_data.stats_.skipped_via_count, 0u);
}

TEST(DSNetGeometryExpandNodeTest, ExpandsViaDataArrayAndSkipsUndefined) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    // VIADATA 阵列（special net）：2×1 @ step 400 → 2 个 via instance；
    // 未定义 via 引用：跳过 + 计数（DSGN::0008 数据源，不 raise）
    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.is_special = true;
    ctx.vias.push_back({"VIA12", 0, 800, 2, 1, 400, 0});
    ctx.vias.push_back({"GHOST_VIA", 0, 0, 1, 1, 0, 0});

    DSNetConnectionParseNode conn;
    DSNetGeometryExpandNode geo;
    conn.handle(ctx);
    geo.handle(ctx);

    ASSERT_EQ(net_data.via_instances_.size(), 2u);
    EXPECT_EQ(net_data.via_instances_.at(1).pos_.get_x(), 0);
    EXPECT_EQ(net_data.via_instances_.at(2).pos_.get_x(), 400);
    EXPECT_EQ(net_data.stats_.via_instance_count, 2u);
    EXPECT_EQ(net_data.stats_.skipped_via_count, 1u);
    EXPECT_EQ(net_data.next_via_instance_id_, 3u);
}

// ── 4. DensityNode：金属/通孔逐层分列通道（⑥）───────────────────────

TEST(DSNetDensityNodeTest, AccumulatesMetalAndViaChannelsPerLayer) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;
    net_data.density_.configure(0, 0, 1000, 1000, 4, 2);

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    // wire 段 (200,200)-(1000,200) 宽 140 → 矩形 (130,130)-(1070,270)
    // → 跨格 col 0..1 row 0 → M1 金属通道 +2
    ctx.wires.push_back({"M1", 140, {GEOPoint(200, 200),
                                     GEOPoint(1000, 200)}});
    // rect (−560,−320)-(4240,320) → col 0..3（clamp）row 0 → M2 +4
    ctx.rects.push_back({"M2", GEORect(-560, -320, 4240, 320)});
    // via instance @ (500,500)：tech VIA12 cut ±100 → (400,400)-(600,600)
    // → 1 格 → VIA1 通孔通道 +1
    ctx.vias.push_back({"VIA12", 500, 500, 1, 1, 0, 0});

    DSNetPipeline pipeline = ds_make_nets_pipeline();
    pipeline.run(ctx);

    // 分类分层保存（⑥）：金属按 wire/rect 所在层、通孔按 via cut 层
    EXPECT_EQ(net_data.density_.layer_total(0, false), 2);  // M1 金属
    EXPECT_EQ(net_data.density_.layer_total(2, false), 4);  // M2 金属
    EXPECT_EQ(net_data.density_.layer_total(1, true), 1);   // VIA1 通孔
    EXPECT_EQ(net_data.density_.metal_total(), 6);
    EXPECT_EQ(net_data.density_.via_total(), 1);
    // 实例面积通道（counts_）不被网侧写入
    EXPECT_EQ(net_data.density_.total_count(), 0);
}

// ── 5. DSNetBuildData 序列化往返（⑬ 独立对象，落盘能力锚定）─────────

TEST(DSNetBuildDataTest, SerializeRoundTrip) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;
    net_data.density_.configure(0, 0, 1000, 1000, 4, 2);

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"u1", "A"});
    ctx.connections.push_back({"PIN", "PIN_A"});
    ctx.wires.push_back({"M1", 140, {GEOPoint(200, 200),
                                     GEOPoint(1000, 200)}});
    ctx.rects.push_back({"M2", GEORect(0, 0, 500, 500)});
    ctx.vias.push_back({"VIA12", 1000, 600, 1, 1, 0, 0});
    ds_make_nets_pipeline().run(ctx);

    CMString blob;
    FLY_ENCODE(net_data, blob);
    DSNetBuildData back;
    FLY_DECODE(blob, DSNetBuildData, back);

    EXPECT_EQ(back.get_block_name(), "");
    ASSERT_EQ(back.connections_.at(1).size(), 2u);
    EXPECT_TRUE(back.connections_.at(1)[1].is_port_ref());
    ASSERT_EQ(back.wires_.at(1).size(), 1u);
    EXPECT_EQ(back.wires_.at(1)[0].layer_id_, 0u);
    EXPECT_EQ(back.wires_.at(1)[0].width_, 140);
    EXPECT_EQ(back.wires_.at(1)[0].points_.size(), 2u);
    ASSERT_EQ(back.rects_.at(1).size(), 1u);
    ASSERT_EQ(back.via_instances_.size(), 1u);
    EXPECT_EQ(back.via_instances_.at(1).via_cell_id_,
              env.design.via_cell_names_.get_id("VIA12"));
    EXPECT_EQ(back.net_via_ids_.at(1).size(), 1u);
    // M1 wire 段 2 格 + M2 rect 1 格；via cut (900,500)-(1100,700) 跨
    // col 0/1 边界 → 2 格
    EXPECT_EQ(back.density_.metal_total(), 3);
    EXPECT_EQ(back.density_.via_total(), 2);
    EXPECT_EQ(back.stats_.net_count, 1u);
    EXPECT_EQ(back.stats_.connection_count, 2u);
    EXPECT_EQ(back.stats_.wire_count, 1u);
    EXPECT_EQ(back.stats_.rect_count, 1u);
    EXPECT_EQ(back.stats_.via_instance_count, 1u);
    EXPECT_EQ(back.next_via_instance_id_, 2u);
}

// ── 6. 适配层全链：真 DEF 网内容（分批 2 强制多批落批）───────────────

// nets_synth.def（UNITS 1000 / stack 2000 = ×2）：
//   n1  : 2 连接 + 2 wire（M1 缺省宽 140）+ 1 via（tech VIA12 plain 名）
//   n2  : 2 连接（无几何）
//   n3  : 1 连接（无几何）
//   VDD : 1 连接 + 2 special wire（显式宽 400）+ 1 RECT + 1 via
//         （⑫ nets_blk::VIADEF1 前缀回退解析）+ 1 未定义 via 兜底跳过
TEST(DsDefNetsTest, ParsesNetContentInBatches) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    // 补齐 def 内其余网名（S5a 网名扫描产物形态：id 按出现序）
    block_data.register_net("n2");
    block_data.register_net("n3");

    DSNetBuildData net_data;
    DSDefNetsStats stats;
    ds_parse_def_nets(test_data("nets_synth.def").string(), env.stack,
                     env.design, block_data, net_data, stats, 1000, 2);

    // 分批：4 网 / 批大小 2 → 2 批（③ 分批落批，产物与单批一致）
    EXPECT_EQ(stats.batch_count, 2);
    EXPECT_EQ(stats.net_count, 4);
    EXPECT_EQ(stats.connection_count, 6);
    EXPECT_EQ(stats.wire_count, 4);
    EXPECT_EQ(stats.rect_count, 1);
    EXPECT_EQ(stats.via_instance_count, 2);
    EXPECT_EQ(stats.skipped_via_count, 1);
    EXPECT_EQ(stats.skipped_net_count, 0);

    // n1：连接 + wire（缺省宽回填）+ via instance
    ASSERT_EQ(net_data.connections_.at(1).size(), 2u);
    ASSERT_EQ(net_data.wires_.at(1).size(), 2u);
    EXPECT_EQ(net_data.wires_.at(1)[0].layer_id_, 0u);
    EXPECT_EQ(net_data.wires_.at(1)[0].width_, 140);
    EXPECT_EQ(net_data.wires_.at(1)[0].points_[0].get_x(), 200);
    EXPECT_EQ(net_data.wires_.at(1)[1].points_[1].get_y(), 600);
    ASSERT_EQ(net_data.via_instances_.size(), 2u);
    EXPECT_EQ(net_data.via_instances_.at(1).pos_.get_x(), 1000);
    EXPECT_EQ(net_data.via_instances_.at(1).pos_.get_y(), 600);

    // VDD（special）：显式宽度 + RECT + 前缀 via + 未定义 via 跳过
    const uint32_t vdd_id = block_data.net_names_->get_id("VDD");
    ASSERT_EQ(net_data.wires_.at(vdd_id).size(), 2u);
    EXPECT_EQ(net_data.wires_.at(vdd_id)[0].width_, 400);
    ASSERT_EQ(net_data.rects_.at(vdd_id).size(), 1u);
    EXPECT_EQ(net_data.rects_.at(vdd_id)[0].rect_.get_x_high(), 4240);
    EXPECT_EQ(net_data.via_instances_.at(2).via_cell_id_,
              env.design.via_cell_names_.get_id("nets_blk::VIADEF1"));

    // 网侧密度（⑥）：格网由 DIEAREA 配置 (0,0)-(4000,2000) bin 1000 → 4×2
    // 金属：n1 两段各跨 col 边界（6）+ VDD 两 wire（4）+ RECT（4）→ M1 6
    // + M2 6 = 12；通孔：VIA12 cut 跨 col 边界（2）+ VIADEF1（1）= 3
    EXPECT_EQ(net_data.density_.get_cols(), 4u);
    EXPECT_EQ(net_data.density_.get_rows(), 2u);
    EXPECT_EQ(net_data.density_.layer_total(0, false), 6);  // M1 金属
    EXPECT_EQ(net_data.density_.layer_total(2, false), 6);  // M2 金属
    EXPECT_EQ(net_data.density_.metal_total(), 12);
    EXPECT_EQ(net_data.density_.via_total(), 3);
}

TEST(DsDefNetsTest, UnreadableFileRaises) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;
    DSDefNetsStats stats;
    EXPECT_THROW(ds_parse_def_nets("/nonexistent/no.def", env.stack,
                                  env.design, block_data, net_data, stats,
                                  1000, 1000),
                 std::runtime_error);
}

}  // namespace
