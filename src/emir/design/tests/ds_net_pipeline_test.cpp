// S5b 网内容责任链 + 批处理单测（裁定 ③④⑨⑩⑪⑫，方案
// design-db-phase2-plan.md §2.3 + design-db-plan.md S5b 节）：
//   1. 链骨架：顺序执行 + 错误标记即停（仿 S5a §2.1 形态）；
//   2. ConnectionParseNode：local net id 对齐（⑨ S5a namemap）+ 连接项
//      id 换算（2026-09-13 裁定：instance 名 → local id、pin 名 → 全局
//      pin id 经组合键、("PIN", port) → port 位 + local 0）+ flags 六位
//      填写（direction/type 顺手取得；hybrid = driver+receiver 同置）+
//      未命中兜底跳过 + 计数 + 未收录网兜底计数；
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

// 标准环境：tech lef 层表（M1=0/VIA1=1/M2=2，恒基准 M1 缺省宽
// 0.07µm×1000=70）+ INV_X1 cell + tech VIA12（plain 名权威表条目）+
// block 前缀 via。
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
        prefixed.set_bottom_layer_id(CMLayerId{0});
        prefixed.set_top_layer_id(CMLayerId{2});
        prefixed.set_cut_layer_id(CMLayerId{1});
        prefixed.add_cut_rect(GEORect(-80, -80, 80, 80));
        design.add_via_cell(std::move(prefixed));

        DSCell inv;
        inv.set_name("INV_X1");
        inv.set_lef_cell();
        inv.set_bbox(GEORect(0, 0, 1400, 1400));
        DSPin a;  // INPUT SIGNAL → 连接 flags receiver 位
        a.direction_ = DSPinDirection::INPUT;
        inv.add_pin(std::move(a));
        DSPin zn;  // OUTPUT SIGNAL → driver 位
        zn.direction_ = DSPinDirection::OUTPUT;
        inv.add_pin(std::move(zn));
        DSPin vdd;  // INOUT POWER → hybrid（driver+receiver 同置）+ power 位
        vdd.direction_ = DSPinDirection::INOUT;
        vdd.type_ = DSPinType::POWER;
        inv.add_pin(std::move(vdd));
        // pin 组合键注册（S2 汇总同构；全局平铺 id 手工分配）
        DSPin vss;  // INPUT GROUND → receiver + ground 位
        vss.direction_ = DSPinDirection::INPUT;
        vss.type_ = DSPinType::GROUND;
        inv.add_pin(std::move(vss));
        DSPin clk;  // INPUT CLOCK → receiver + clock 位（USE CLOCK 收录）
        clk.direction_ = DSPinDirection::INPUT;
        clk.type_ = DSPinType::CLOCK;
        inv.add_pin(std::move(clk));
        design.add_cell(std::move(inv));
        // pin 组合键注册（S2 汇总同构；全局平铺 id 手工分配）
        design.register_pin("A");  // id 0
        design.register_pin("ZN");  // id 1
        design.register_pin("VDD");  // id 2
        design.register_pin("VSS");  // id 3
        design.register_pin("CLK");  // id 4
        design.cells_[0].pins_[0].set_pin_id(CMPinId{0});
        design.cells_[0].pins_[1].set_pin_id(CMPinId{1});
        design.cells_[0].pins_[2].set_pin_id(CMPinId{2});
        design.cells_[0].pins_[3].set_pin_id(CMPinId{3});
        design.cells_[0].pins_[4].set_pin_id(CMPinId{4});

        // block cell（S4 汇总同构）：port pin 的方向/type 是连接 flags
        // port 条目位填写的数据源
        DSCell blk;
        blk.set_name("nets_blk");
        blk.set_block_cell();
        DSPin pin_a;  // INPUT → port 条目 receiver 位
        pin_a.set_port();
        pin_a.direction_ = DSPinDirection::INPUT;
        blk.add_pin(std::move(pin_a));
        DSPin pout;  // OUTPUT → port 条目 driver 位
        pout.set_port();
        pout.direction_ = DSPinDirection::OUTPUT;
        blk.add_pin(std::move(pout));
        design.add_cell(std::move(blk));
        design.register_pin("PIN_A");  // id 5
        design.register_pin("POUT");  // id 6
        design.cells_[1].pins_[0].set_pin_id(CMPinId{5});
        design.cells_[1].pins_[1].set_pin_id(CMPinId{6});
    }

    // 挂好环境的 ctx（local 0 占位 + 实例 u1 = local 1 + 两网名，仿 S5a
    // 产物；连接 id 换算需实例 hasher，2026-09-13 裁定）
    DSBlockBuildData make_block_data() const {
        DSBlockBuildData block_data;
        block_data.init_placeholder("nets_blk", CMCellId{});
        DSInstance u1;
        u1.set_cell_id(CMCellId{design.cell_names_.get_id("INV_X1")});
        block_data.add_instance(std::move(u1), "u1");
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

TEST(DSNetConnectionParseNodeTest, AlignsLocalIdAndConvertsConnectionIds) {
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
    ASSERT_EQ(net_data.connections_.at(CMNetId{1})->size(), 2u);
    // 连接项 id 形态（2026-09-13 裁定）：(instance local id, 全局 pin id)
    // + flags 位——(u1 A)：local 1、pin A = 0、INPUT → receiver 位
    const DSNetConnection& c0 = (*net_data.connections_.at(CMNetId{1}))[0];
    EXPECT_EQ(c0.instance_local_id_, 1u);
    EXPECT_EQ(c0.pin_id_, 0u);
    EXPECT_FALSE(c0.is_port());
    EXPECT_TRUE(c0.is_receiver());
    EXPECT_FALSE(c0.is_driver());
    // ("PIN" PIN_A)：local 0 占位（⑧）、port pin = 3、port 位 + INPUT
    // → receiver 位
    const DSNetConnection& c1 = (*net_data.connections_.at(CMNetId{1}))[1];
    EXPECT_EQ(c1.instance_local_id_, 0u);
    EXPECT_EQ(c1.pin_id_, 5u);
    EXPECT_TRUE(c1.is_port());
    EXPECT_TRUE(c1.is_receiver());
    EXPECT_EQ(net_data.stats_.net_count, 1u);
    EXPECT_EQ(net_data.stats_.connection_count, 2u);
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 0u);
}

TEST(DSNetConnectionParseNodeTest, FillsDriverReceiverPowerFlags) {
    // 方向/类型位（2026-09-13 裁定变更 + 补充）：OUTPUT → driver、INOUT
    // → driver+receiver 同置（hybrid）、POWER type → power 位；port 条目
    // 方向随 block cell 的 port pin（POUT = OUTPUT → driver）
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.is_special = true;
    ctx.connections.push_back({"u1", "VDD"});   // INOUT POWER → hybrid+power
    ctx.connections.push_back({"PIN", "POUT"});  // port + OUTPUT → driver
    ctx.connections.push_back({"u1", "ZN"});     // OUTPUT → 仅 driver
    ctx.connections.push_back({"u1", "VSS"});    // INPUT GROUND → receiver+ground
    ctx.connections.push_back({"u1", "CLK"});    // INPUT CLOCK → receiver+clock

    DSNetConnectionParseNode node;
    node.handle(ctx);

    ASSERT_EQ(net_data.connections_.at(CMNetId{2})->size(), 5u);
    const DSNetConnection& hybrid = (*net_data.connections_.at(CMNetId{2}))[0];
    EXPECT_EQ(hybrid.instance_local_id_, 1u);
    EXPECT_EQ(hybrid.pin_id_, 2u);  // INV_X1/VDD
    EXPECT_TRUE(hybrid.is_driver() && hybrid.is_receiver());  // hybrid
    EXPECT_TRUE(hybrid.is_power());
    EXPECT_FALSE(hybrid.is_ground());
    const DSNetConnection& port = (*net_data.connections_.at(CMNetId{2}))[1];
    EXPECT_EQ(port.instance_local_id_, 0u);
    EXPECT_EQ(port.pin_id_, 6u);  // nets_blk/POUT
    EXPECT_TRUE(port.is_port() && port.is_driver());
    EXPECT_FALSE(port.is_receiver());
    const DSNetConnection& drv = (*net_data.connections_.at(CMNetId{2}))[2];
    EXPECT_EQ(drv.pin_id_, 1u);  // INV_X1/ZN
    EXPECT_TRUE(drv.is_driver());
    EXPECT_FALSE(drv.is_receiver());
    EXPECT_FALSE(drv.is_driver() && drv.is_receiver());  // 非 hybrid
    // ground 位（INPUT GROUND → receiver + ground，与 power 互斥）
    const DSNetConnection& gnd = (*net_data.connections_.at(CMNetId{2}))[3];
    EXPECT_EQ(gnd.pin_id_, 3u);  // INV_X1/VSS
    EXPECT_TRUE(gnd.is_receiver() && gnd.is_ground());
    EXPECT_FALSE(gnd.is_power() || gnd.is_driver() || gnd.is_clock());
    // clock 位（INPUT CLOCK → receiver + clock）
    const DSNetConnection& clk = (*net_data.connections_.at(CMNetId{2}))[4];
    EXPECT_EQ(clk.pin_id_, 4u);  // INV_X1/CLK
    EXPECT_TRUE(clk.is_receiver() && clk.is_clock());
    EXPECT_FALSE(clk.is_power() || clk.is_ground() || clk.is_driver());
}

TEST(DSNetConnectionParseNodeTest, InvalidConnectionsSkippedAndCounted) {
    // 未命中兜底（2026-09-13 id 化裁定，dev-rules §7 不 raise）：实例名
    // 未登记 / cell 无此 pin / port 未注册——各自跳过 + 计数 + DSGN::0025
    // 提醒，合法条目照常收录
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"ghost_inst", "A"});   // 实例名未登记
    ctx.connections.push_back({"u1", "GHOST_PIN"});   // cell 无此 pin
    ctx.connections.push_back({"PIN", "GHOST_PORT"});  // port 未注册
    ctx.connections.push_back({"u1", "A"});           // 合法条目

    DSNetConnectionParseNode node;
    node.handle(ctx);

    EXPECT_FALSE(ctx.error);
    ASSERT_EQ(net_data.connections_.at(CMNetId{1})->size(), 1u);
    EXPECT_EQ((*net_data.connections_.at(CMNetId{1}))[0].instance_local_id_, 1u);
    EXPECT_EQ((*net_data.connections_.at(CMNetId{1}))[0].pin_id_, 0u);
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 3u);
    EXPECT_EQ(net_data.stats_.connection_count, 1u);
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

    // 普通 net 无显式宽度（width_dbu = 0）→ 回填 M1 层缺省宽 70
    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.wires.push_back({"M1", 0, {GEOPoint(200, 200),
                                   GEOPoint(1000, 200)}});

    DSNetConnectionParseNode conn;
    DSNetGeometryExpandNode geo;
    conn.handle(ctx);
    geo.handle(ctx);

    ASSERT_EQ(net_data.wires_.at(CMNetId{1})->size(), 1u);
    const DSNetWire& wire = (*net_data.wires_.at(CMNetId{1}))[0];
    EXPECT_EQ(wire.layer_id_, 0u);  // M1
    EXPECT_EQ(wire.width_, 70);     // 0.07 µm × 1000 DBU/µm（tech 缺省宽）
    ASSERT_EQ(wire.points_.size(), 2u);
    EXPECT_EQ(wire.points_[0].get_x(), 200);
    EXPECT_EQ(wire.points_[1].get_y(), 200);
    EXPECT_EQ(net_data.stats_.wire_count, 1u);
}

TEST(DSNetGeometryExpandNodeTest, WireAndRectUndefinedLayerDropped) {
    // 层引用未定义 → 条目级丢弃兜底（dev-rules §7：不 raise + DSGN::0010
    // 提醒）：未定义层的 wire 段 / rect 项各自丢弃 + 计数，合法条目照常
    // 收录
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.wires.push_back({"M1", 0, {GEOPoint(0, 0), GEOPoint(100, 100)}});
    ctx.wires.push_back({"GHOST_LAYER", 0, {GEOPoint(0, 0)}});
    ctx.rects.push_back({"M2", GEORect(0, 0, 100, 100)});
    ctx.rects.push_back({"GHOST_LAYER", GEORect(0, 0, 100, 100)});

    DSNetConnectionParseNode conn;
    DSNetGeometryExpandNode geo;
    conn.handle(ctx);
    geo.handle(ctx);

    EXPECT_FALSE(ctx.error);  // 兜底计数不拦截
    ASSERT_EQ(net_data.wires_.at(CMNetId{1})->size(), 1u);   // 合法 wire 保留
    EXPECT_EQ((*net_data.wires_.at(CMNetId{1}))[0].layer_id_, 0u);
    ASSERT_EQ(net_data.rects_.at(CMNetId{1})->size(), 1u);   // 合法 rect 保留
    EXPECT_EQ((*net_data.rects_.at(CMNetId{1}))[0].layer_id_, 2u);
    EXPECT_EQ(net_data.stats_.wire_count, 1u);
    EXPECT_EQ(net_data.stats_.rect_count, 1u);
    EXPECT_EQ(net_data.stats_.skipped_layer_ref_count, 2u);
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

    ASSERT_EQ(net_data.wires_.at(CMNetId{2})->size(), 1u);
    EXPECT_EQ((*net_data.wires_.at(CMNetId{2}))[0].layer_id_, 2u);  // M2
    EXPECT_EQ((*net_data.wires_.at(CMNetId{2}))[0].width_, 400);    // 显式宽度不回填
    ASSERT_EQ(net_data.rects_.at(CMNetId{2})->size(), 1u);
    EXPECT_EQ((*net_data.rects_.at(CMNetId{2}))[0].layer_id_, 2u);
    EXPECT_EQ((*net_data.rects_.at(CMNetId{2}))[0].rect_.get_x_high(), 4240);
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
    const DSViaInstance& v1 = (*net_data.via_instances_.at(CMViaInstanceId{1}));
    EXPECT_EQ(v1.via_cell_id_, env.design.via_cell_names_.get_id("VIA12"));
    EXPECT_EQ(v1.pos_.get_x(), 1000);
    EXPECT_EQ(v1.pos_.get_y(), 600);
    const DSViaInstance& v2 = (*net_data.via_instances_.at(CMViaInstanceId{2}));
    EXPECT_EQ(v2.via_cell_id_,
              env.design.via_cell_names_.get_id("nets_blk::VIADEF1"));
    EXPECT_EQ(v2.pos_.get_y(), 1600);
    // 网归属表
    ASSERT_EQ((*net_data.net_via_ids_.at(CMNetId{1})).size(), 2u);
    EXPECT_EQ((*net_data.net_via_ids_.at(CMNetId{1}))[0], 1u);
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
    EXPECT_EQ((*net_data.via_instances_.at(CMViaInstanceId{1})).pos_.get_x(), 0);
    EXPECT_EQ((*net_data.via_instances_.at(CMViaInstanceId{2})).pos_.get_x(), 400);
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
    EXPECT_EQ(net_data.density_.layer_total(CMLayerId{0}, false), 2);  // M1 金属
    EXPECT_EQ(net_data.density_.layer_total(CMLayerId{2}, false), 4);  // M2 金属
    EXPECT_EQ(net_data.density_.layer_total(CMLayerId{1}, true), 1);   // VIA1 通孔
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
    ctx.use = DSNetUse::SCAN;  // use 收录（非 SIGNAL 才落存储）
    ds_make_nets_pipeline().run(ctx);

    CMString blob;
    FLY_ENCODE(net_data, blob);
    DSNetBuildData back;
    FLY_DECODE(blob, DSNetBuildData, back);

    EXPECT_EQ(back.get_block_name(), "");
    ASSERT_EQ(back.connections_.at(CMNetId{1})->size(), 2u);
    EXPECT_EQ((*back.connections_.at(CMNetId{1}))[0].instance_local_id_, 1u);
    EXPECT_EQ((*back.connections_.at(CMNetId{1}))[0].pin_id_, 0u);
    EXPECT_TRUE((*back.connections_.at(CMNetId{1}))[1].is_port());
    EXPECT_EQ((*back.connections_.at(CMNetId{1}))[1].pin_id_, 5u);
    ASSERT_EQ(back.wires_.at(CMNetId{1})->size(), 1u);
    EXPECT_EQ((*back.wires_.at(CMNetId{1}))[0].layer_id_, 0u);
    EXPECT_EQ((*back.wires_.at(CMNetId{1}))[0].width_, 140);
    EXPECT_EQ((*back.wires_.at(CMNetId{1}))[0].points_.size(), 2u);
    ASSERT_EQ(back.rects_.at(CMNetId{1})->size(), 1u);
    ASSERT_EQ(back.via_instances_.size(), 1u);
    EXPECT_EQ((*back.via_instances_.at(CMViaInstanceId{1})).via_cell_id_,
              env.design.via_cell_names_.get_id("VIA12"));
    EXPECT_EQ((*back.net_via_ids_.at(CMNetId{1})).size(), 1u);
    // M1 wire 段 2 格 + M2 rect 1 格；via cut (900,500)-(1100,700) 跨
    // col 0/1 边界 → 2 格
    EXPECT_EQ(back.density_.metal_total(), 3);
    EXPECT_EQ(back.density_.via_total(), 2);
    EXPECT_EQ(back.stats_.net_count, 1u);
    EXPECT_EQ(back.stats_.connection_count, 2u);
    EXPECT_EQ(back.stats_.wire_count, 1u);
    EXPECT_EQ(back.stats_.rect_count, 1u);
    EXPECT_EQ(back.stats_.via_instance_count, 1u);
    EXPECT_EQ(back.next_via_instance_id_, CMViaInstanceId{2});
    // use 往返（2026-09-13 USE 全量补收：非 SIGNAL 条目随产物序列化）
    EXPECT_EQ(back.net_use_of(CMNetId{1}), DSNetUse::SCAN);
    EXPECT_EQ(back.net_use_of(CMNetId{2}), DSNetUse::SIGNAL);  // 未记录 = 缺省
}

// USE 文本解析纯函数（2026-09-13 USE 全量补收：八值全集 + 未知兜底 +
// 字符串化）
TEST(DSNetUseParseTest, ParsesAllEightDefUseValues) {
    bool known = false;
    EXPECT_EQ(ds_parse_net_use("SIGNAL", &known), DSNetUse::SIGNAL);
    EXPECT_TRUE(known);
    EXPECT_EQ(ds_parse_net_use("POWER", &known), DSNetUse::POWER);
    EXPECT_EQ(ds_parse_net_use("GROUND", &known), DSNetUse::GROUND);
    EXPECT_EQ(ds_parse_net_use("CLOCK", &known), DSNetUse::CLOCK);
    EXPECT_EQ(ds_parse_net_use("TIEOFF", &known), DSNetUse::TIEOFF);
    EXPECT_EQ(ds_parse_net_use("ANALOG", &known), DSNetUse::ANALOG);
    EXPECT_EQ(ds_parse_net_use("RESET", &known), DSNetUse::RESET);
    EXPECT_EQ(ds_parse_net_use("SCAN", &known), DSNetUse::SCAN);
    EXPECT_TRUE(known);
}

TEST(DSNetUseParseTest, UnknownAndEmptyFallBackToSignal) {
    bool known = true;
    EXPECT_EQ(ds_parse_net_use("BOGUS", &known), DSNetUse::SIGNAL);
    EXPECT_FALSE(known);  // 未知值 → SIGNAL 兜底 + known 上报（计数源）
    EXPECT_EQ(ds_parse_net_use("", &known), DSNetUse::SIGNAL);
    EXPECT_FALSE(known);
    EXPECT_EQ(ds_parse_net_use(nullptr, nullptr), DSNetUse::SIGNAL);
    // 大小写保留精确匹配（namemap 同口径）：小写不命中
    EXPECT_EQ(ds_parse_net_use("power", &known), DSNetUse::SIGNAL);
    EXPECT_FALSE(known);
}

TEST(DSNetUseParseTest, NameRoundTrip) {
    for (const DSNetUse use : {DSNetUse::SIGNAL, DSNetUse::POWER,
                               DSNetUse::GROUND, DSNetUse::CLOCK,
                               DSNetUse::TIEOFF, DSNetUse::ANALOG,
                               DSNetUse::RESET, DSNetUse::SCAN}) {
        EXPECT_EQ(ds_parse_net_use(ds_net_use_name(use)), use);
    }
}

// ── 6. 适配层全链：真 DEF 网内容（分批 2 强制多批落批）───────────────

// nets_synth.def（UNITS 1000 / 恒基准 1000 = ×1）：
//   n1  : 2 连接 + 2 wire（M1 缺省宽 70）+ 1 via（tech VIA12 plain 名）
//         （无 USE 语句——非 SIGNAL 才落存储的缺省 SIGNAL 对照组）
//   n2  : 2 连接（无几何）+ USE CLOCK（收录）
//   n3  : 1 连接（无几何）+ USE TIEOFF（收录）
//   VDD : 1 连接 + 2 special wire（显式宽 200）+ 1 RECT + 1 via
//         （⑫ nets_blk::VIADEF1 前缀回退解析）+ 1 未定义 via 兜底跳过
//         + USE POWER（special 位 ∨ use 位 → pg 派生）
// 未知 USE 值形态：defi 解析器语法层即校验 USE 值（DEFPARS-5500，
// DSGN::0016 fatal 域）——真 DEF 路径到不了业务兜底；ds_parse_net_use
// 的未知值兜底语义由 DSNetUseParseTest 纯函数单测锁定。
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
    EXPECT_EQ(stats.skipped_layer_ref_count, 0);

    // n1：连接 + wire（缺省宽回填）+ via instance
    ASSERT_EQ(net_data.connections_.at(CMNetId{1})->size(), 2u);
    ASSERT_EQ(net_data.wires_.at(CMNetId{1})->size(), 2u);
    EXPECT_EQ((*net_data.wires_.at(CMNetId{1}))[0].layer_id_, 0u);
    EXPECT_EQ((*net_data.wires_.at(CMNetId{1}))[0].width_, 70);
    EXPECT_EQ((*net_data.wires_.at(CMNetId{1}))[0].points_[0].get_x(), 100);
    EXPECT_EQ((*net_data.wires_.at(CMNetId{1}))[1].points_[1].get_y(), 300);
    ASSERT_EQ(net_data.via_instances_.size(), 2u);
    EXPECT_EQ((*net_data.via_instances_.at(CMViaInstanceId{1})).pos_.get_x(), 500);
    EXPECT_EQ((*net_data.via_instances_.at(CMViaInstanceId{1})).pos_.get_y(), 300);

    // VDD（special）：显式宽度 + RECT + 前缀 via + 未定义 via 跳过
    const CMNetId vdd_id{block_data.net_names_->get_id("VDD")};
    ASSERT_EQ(net_data.wires_.at(vdd_id)->size(), 2u);
    EXPECT_EQ((*net_data.wires_.at(vdd_id))[0].width_, 200);
    ASSERT_EQ(net_data.rects_.at(vdd_id)->size(), 1u);
    EXPECT_EQ((*net_data.rects_.at(vdd_id))[0].rect_.get_x_high(), 2120);
    EXPECT_EQ((*net_data.via_instances_.at(CMViaInstanceId{2})).via_cell_id_,
              env.design.via_cell_names_.get_id("nets_blk::VIADEF1"));

    // 网侧密度（⑥）：格网由 DIEAREA 配置 (0,0)-(2000,1000) bin 1000 → 2×1
    // 金属：n1 两段 wire 各 1 格 + VDD M1 wire 1 格（M1 3）+ VDD M2 wire
    // 1 格 + RECT 跨 col 边界 2 格（M2 3）→ M1 3 + M2 3 = 6；通孔：
    // VIA12（1）+ VIADEF1（1）= 2
    EXPECT_EQ(net_data.density_.get_cols(), 2u);
    EXPECT_EQ(net_data.density_.get_rows(), 1u);
    EXPECT_EQ(net_data.density_.layer_total(CMLayerId{0}, false), 3);  // M1 金属
    EXPECT_EQ(net_data.density_.layer_total(CMLayerId{2}, false), 3);  // M2 金属
    EXPECT_EQ(net_data.density_.metal_total(), 6);
    EXPECT_EQ(net_data.density_.via_total(), 2);

    // USE 全量补收（2026-09-13 裁定）：n2 CLOCK / n3 TIEOFF / VDD POWER
    // 收录；n1 无 USE 语句 → 缺省 SIGNAL 不落存储；pg 派生（VDD =
    // special ∨ USE POWER）；真 DEF 无未知值（defi 语法层拦截）→ 计数 0
    EXPECT_EQ(stats.unknown_use_count, 0);
    EXPECT_EQ(net_data.stats_.unknown_use_count, 0u);
    EXPECT_EQ(net_data.net_use_of(CMNetId{1}), DSNetUse::SIGNAL);
    EXPECT_EQ(net_data.net_uses_.size(), 3u);  // 只记非 SIGNAL 条目
    EXPECT_EQ(net_data.net_use_of(
                  CMNetId{block_data.net_names_->get_id("n2")}),
              DSNetUse::CLOCK);
    EXPECT_EQ(net_data.net_use_of(
                  CMNetId{block_data.net_names_->get_id("n3")}),
              DSNetUse::TIEOFF);
    const CMNetId vdd_use_id{block_data.net_names_->get_id("VDD")};
    EXPECT_EQ(net_data.net_use_of(vdd_use_id), DSNetUse::POWER);
    EXPECT_TRUE(net_data.is_pg_net(CMNetId{vdd_use_id}));
    EXPECT_FALSE(net_data.is_pg_net(
        CMNetId{block_data.net_names_->get_id("n3")}));  // TIEOFF 非 pg
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
