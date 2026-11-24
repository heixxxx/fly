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

#include <common/testing/cpp/test_helpers.h>

#include <gtest/gtest.h>

#include "borrow_view.h"

#include <message/cpp/message_macros.h>
#include <message/cpp/message_registry.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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

        // BUF_X1（通配展开的混合单元对照面：A INPUT / Z OUTPUT，无电源
        // 地引脚——「谁有谁连」分流依据）。A 与 INV_X1 共享全局 id（裁
        // 定 3 同名 pin 共享 id），Z 新登记
        DSCell buf;
        buf.set_name("BUF_X1");
        buf.set_lef_cell();
        buf.set_bbox(GEORect(0, 0, 1400, 1400));
        DSPin b_a;  // INPUT SIGNAL（共享全局 id 0）
        b_a.direction_ = DSPinDirection::INPUT;
        buf.add_pin(std::move(b_a));
        DSPin b_z;  // OUTPUT SIGNAL → driver 位
        b_z.direction_ = DSPinDirection::OUTPUT;
        buf.add_pin(std::move(b_z));
        design.add_cell(std::move(buf));
        design.register_pin("Z");  // id 7
        design.cells_[2].pins_[0].set_pin_id(CMPinId{0});  // BUF A = 共享 0
        design.cells_[2].pins_[1].set_pin_id(CMPinId{7});  // BUF Z
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

    // 通配展开测试的实例环境（仿 S5a 产物形态）：u1/u2 = INV_X1（有
    // VDD/VSS/A/ZN/CLK）、u3 = BUF_X1（无电源地引脚）。local id 分配序
    // 1/2/3（add_instance 顺序，⑧ 从 1 起）。
    DSBlockBuildData make_multi_block_data() const {
        DSBlockBuildData block_data;
        block_data.init_placeholder("nets_blk", CMCellId{});
        DSInstance u1;
        u1.set_cell_id(CMCellId{design.cell_names_.get_id("INV_X1")});
        block_data.add_instance(std::move(u1), "u1");
        DSInstance u2;
        u2.set_cell_id(CMCellId{design.cell_names_.get_id("INV_X1")});
        block_data.add_instance(std::move(u2), "u2");
        DSInstance u3;
        u3.set_cell_id(CMCellId{design.cell_names_.get_id("BUF_X1")});
        block_data.add_instance(std::move(u3), "u3");
        block_data.register_net("n1");
        block_data.register_net("VDD");
        return block_data;
    }
};

DSNetContext make_ctx(const TestEnv& env, const DSBlockBuildData& block_data,
                      DSNetBuildData& net_data, const char* net_name) {
    DSNetContext ctx;
    ctx.net_name = net_name;
    ctx.stack = test::borrow(env.stack);
    ctx.design = test::borrow(env.design);
    ctx.block_data = test::borrow(block_data);
    ctx.net_data = test::borrow(net_data);
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
    // 2026-11-24 连接表唯一性裁定：读前封存（模拟生产解析入口尾部封存点）
    net_data.seal_all_connections();

    // ⑨ local net id 沿用 S5a 网名扫描分配（n1 = 1）
    EXPECT_EQ(ctx.local_net_id, 1u);
    ASSERT_EQ(net_data.connections_.at(CMNetId{1})->size(), 2u);
    // 连接项 id 形态（2026-09-13 裁定）：(instance local id, 全局 pin id)
    // + flags 位；封存后按 (实例, 引脚) 升序——端口条目（local 0 占位）
    // 排最前。("PIN" PIN_A)：local 0 占位（⑧）、port pin = 5、port 位 +
    // INPUT 端口 = 它所在网的驱动者（2026-11-24 用户裁定：端口条目按网
    // 的角色反向标——driver 位）
    const DSNetConnection& c0 = (*net_data.connections_.at(CMNetId{1}))[0];
    EXPECT_EQ(c0.instance_local_id_, 0u);
    EXPECT_EQ(c0.pin_id_, 5u);
    EXPECT_TRUE(c0.is_port());
    EXPECT_TRUE(c0.is_driver());
    EXPECT_FALSE(c0.is_receiver());
    // (u1 A)：local 1、pin A = 0、INPUT → receiver 位（实例引脚条目不变
    // ——按引脚方向 = 在网里的角色）
    const DSNetConnection& c1 = (*net_data.connections_.at(CMNetId{1}))[1];
    EXPECT_EQ(c1.instance_local_id_, 1u);
    EXPECT_EQ(c1.pin_id_, 0u);
    EXPECT_FALSE(c1.is_port());
    EXPECT_TRUE(c1.is_receiver());
    EXPECT_FALSE(c1.is_driver());
    EXPECT_EQ(net_data.stats_.net_count, 1u);
    EXPECT_EQ(net_data.stats_.connection_count, 2u);
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 0u);
}

TEST(DSNetConnectionParseNodeTest, FillsDriverReceiverPowerFlags) {
    // 方向/类型位（2026-09-13 裁定变更 + 补充）：OUTPUT → driver、INOUT
    // → driver+receiver 同置（hybrid）、POWER type → power 位；端口条目
    // 按网的角色反向标（2026-11-24 用户裁定）：POUT = OUTPUT 端口 → 负载
    // （receiver 位），不再沿用引脚方向的 driver
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.is_special = true;
    ctx.connections.push_back({"u1", "VDD"});   // INOUT POWER → hybrid+power
    ctx.connections.push_back({"PIN", "POUT"});  // port + OUTPUT → receiver
    ctx.connections.push_back({"u1", "ZN"});     // OUTPUT → 仅 driver
    ctx.connections.push_back({"u1", "VSS"});    // INPUT GROUND → receiver+ground
    ctx.connections.push_back({"u1", "CLK"});    // INPUT CLOCK → receiver+clock

    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();  // 封存后 (实例, 引脚) 升序

    ASSERT_EQ(net_data.connections_.at(CMNetId{2})->size(), 5u);
    // 升序：端口条目 (0, POUT=6) 最前，随后 (1, ZN=1)、(1, VDD=2)、
    // (1, VSS=3)、(1, CLK=4)
    const DSNetConnection& port = (*net_data.connections_.at(CMNetId{2}))[0];
    EXPECT_EQ(port.instance_local_id_, 0u);
    EXPECT_EQ(port.pin_id_, 6u);  // nets_blk/POUT
    EXPECT_TRUE(port.is_port() && port.is_receiver());
    EXPECT_FALSE(port.is_driver());
    const DSNetConnection& drv = (*net_data.connections_.at(CMNetId{2}))[1];
    EXPECT_EQ(drv.pin_id_, 1u);  // INV_X1/ZN
    EXPECT_TRUE(drv.is_driver());
    EXPECT_FALSE(drv.is_receiver());
    EXPECT_FALSE(drv.is_driver() && drv.is_receiver());  // 非 hybrid
    const DSNetConnection& hybrid = (*net_data.connections_.at(CMNetId{2}))[2];
    EXPECT_EQ(hybrid.instance_local_id_, 1u);
    EXPECT_EQ(hybrid.pin_id_, 2u);  // INV_X1/VDD
    EXPECT_TRUE(hybrid.is_driver() && hybrid.is_receiver());  // hybrid
    EXPECT_TRUE(hybrid.is_power());
    EXPECT_FALSE(hybrid.is_ground());
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

// 端口条目三向标志位（2026-11-24 用户裁定）：输入端口 → 驱动者、输出
// 端口 → 负载、双向端口 → 两者（hybrid）；实例引脚条目不变（Aligns/
// Fills 两例已锁 INPUT/OUTPUT 引脚与 INPUT/OUTPUT 端口，本例补双向端口）
TEST(DSNetConnectionParseNodeTest, InoutPortMarksBothRoles) {
    TestEnv env;
    // block cell 补双向端口 PIN_IO（INOUT；id 8——同轮 INOUT 引脚语义与
    // 既有「INOUT = driver+receiver 同置 = hybrid」一致）
    DSCell& blk = env.design.get_cell(env.design.cell_names_.get_id("nets_blk"));
    DSPin pin_io;
    pin_io.set_port();
    pin_io.direction_ = DSPinDirection::INOUT;
    env.design.register_pin("PIN_IO");
    pin_io.set_pin_id(env.design.pin_names_.get_id("PIN_IO"));
    blk.add_pin(std::move(pin_io));

    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"PIN", "PIN_IO"});
    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{1});
    ASSERT_EQ(conns->size(), 1u);
    EXPECT_TRUE((*conns)[0].is_port());
    EXPECT_TRUE((*conns)[0].is_driver() && (*conns)[0].is_receiver());  // 两者
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
    net_data.seal_all_connections();

    EXPECT_FALSE(ctx.error);
    ASSERT_EQ(net_data.connections_.at(CMNetId{1})->size(), 1u);
    EXPECT_EQ((*net_data.connections_.at(CMNetId{1}))[0].instance_local_id_, 1u);
    EXPECT_EQ((*net_data.connections_.at(CMNetId{1}))[0].pin_id_, 0u);
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 3u);
    EXPECT_EQ(net_data.stats_.connection_count, 1u);
}

// 连接顺序（2026-11-24 连接表唯一性裁定）：解析产物封存后按 (实例局部
// id, 引脚 id) 升序——DEF 书写顺序不再保留；端口条目（local 0 占位）
// 自然排在最前
TEST(DSNetConnectionParseNodeTest, ConnectionsSealedAscending) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    // 乱序书写：u1/ZN(pin 1) → PIN/PIN_A(local 0) → u1/A(pin 0)
    ctx.connections.push_back({"u1", "ZN"});
    ctx.connections.push_back({"PIN", "PIN_A"});
    ctx.connections.push_back({"u1", "A"});
    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{1});
    ASSERT_EQ(conns->size(), 3u);
    EXPECT_EQ((*conns)[0].instance_local_id_, CMInstanceId{0});  // PIN_A
    EXPECT_EQ((*conns)[0].pin_id_, CMPinId{5});
    EXPECT_EQ((*conns)[1].instance_local_id_, CMInstanceId{1});  // u1/A
    EXPECT_EQ((*conns)[1].pin_id_, CMPinId{0});
    EXPECT_EQ((*conns)[2].instance_local_id_, CMInstanceId{1});  // u1/ZN
    EXPECT_EQ((*conns)[2].pin_id_, CMPinId{1});
}

// 两条完全相同的显式条目 → 一条（2026-11-24 连接表唯一性裁定：显式条目
// 间的重复不再「照常收录」，由封存统一去重 + duplicate_connection_count
// 计数）
TEST(DSNetConnectionParseNodeTest, DuplicateExplicitEntriesDeduplicated) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"u1", "A"});
    ctx.connections.push_back({"u1", "A"});  // 与前一条完全相同
    DSNetConnectionParseNode node;
    node.handle(ctx);
    const uint64_t removed = net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{1});
    ASSERT_EQ(conns->size(), 1u);
    EXPECT_EQ((*conns)[0].instance_local_id_, CMInstanceId{1});
    EXPECT_EQ((*conns)[0].pin_id_, CMPinId{0});
    EXPECT_EQ(removed, 1u);
    EXPECT_EQ(net_data.stats_.duplicate_connection_count, 1u);
    // connection_count = 封存后最终收录数（去重同步扣除）
    EXPECT_EQ(net_data.stats_.connection_count, 1u);
}

// 同名网合并（网名扫描保留首份——第二张同名网的连接并入首张网）带来
// 的重复 → 封存后一条
TEST(DSNetConnectionParseNodeTest, SameNameNetMergeDeduplicates) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_block_data();
    DSNetBuildData net_data;

    DSNetConnectionParseNode node;
    // 第一张 VDD 网：u1/VDD
    DSNetContext ctx1 = make_ctx(env, block_data, net_data, "VDD");
    ctx1.connections.push_back({"u1", "VDD"});
    node.handle(ctx1);
    // 第二张同名 VDD 网（register_net 重名保留首份 → 同一 local id）：
    // 再写一条 u1/VDD → 并入首张网的连接表，构成解析期重复
    DSNetContext ctx2 = make_ctx(env, block_data, net_data, "VDD");
    ctx2.connections.push_back({"u1", "VDD"});
    node.handle(ctx2);
    const uint64_t removed = net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{2});
    ASSERT_EQ(conns->size(), 1u);   // 合并重复被去掉，恰一条
    EXPECT_EQ((*conns)[0].instance_local_id_, CMInstanceId{1});
    EXPECT_EQ((*conns)[0].pin_id_, CMPinId{2});
    EXPECT_EQ(removed, 1u);
    EXPECT_EQ(net_data.stats_.duplicate_connection_count, 1u);
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
    net_data.seal_all_connections();  // 封存后再落盘（生产路径解析入口尾部
                                      // 封存——往返后的表直接可读）

    CMString blob;
    FLY_ENCODE(net_data, blob);
    DSNetBuildData back;
    FLY_DECODE(blob, DSNetBuildData, back);

    EXPECT_EQ(back.get_block_name(), "");
    ASSERT_EQ(back.connections_.at(CMNetId{1})->size(), 2u);
    // 升序：端口条目 (0, PIN_A=5) 在前、u1 (1, A=0) 在后
    EXPECT_EQ((*back.connections_.at(CMNetId{1}))[0].instance_local_id_, 0u);
    EXPECT_TRUE((*back.connections_.at(CMNetId{1}))[0].is_port());
    EXPECT_EQ((*back.connections_.at(CMNetId{1}))[0].pin_id_, 5u);
    EXPECT_EQ((*back.connections_.at(CMNetId{1}))[1].instance_local_id_, 1u);
    EXPECT_EQ((*back.connections_.at(CMNetId{1}))[1].pin_id_, 0u);
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
    ds_parse_def_nets(test_data("nets_synth.def").string(),
                      test::borrow(env.stack), test::borrow(env.design),
                      test::borrow(block_data), test::borrow(net_data), stats,
                      1000, 2);

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
    EXPECT_THROW(ds_parse_def_nets("/nonexistent/no.def",
                                  test::borrow(env.stack),
                                  test::borrow(env.design),
                                  test::borrow(block_data),
                                  test::borrow(net_data), stats, 1000, 1000),
                 std::runtime_error);
}

// ── 7. 通配条目 ( * 引脚 ) 展开（2026-11-24 真实数据兼容裁定）────────

// DSGN 消息捕获（注册 id + 不限配额——「未发出」断言才可信；析构复位
// 推送口，仿 master_agent_test 先例）
class MessageCapture {
public:
    explicit MessageCapture(const char* id) {
        fly::MessageRegistry::instance().register_id(id,
                                                    fly::LogLevel::WARN);
        fly::MessageRegistry::instance().set_id_limit(id, -1);
        fly::set_message_push_func(
            [this](fly::LogLevel, const CMString& domain, int32_t,
                   const CMString&) { ids_.push_back(domain); });
    }
    ~MessageCapture() { fly::set_message_push_func(nullptr); }
    MessageCapture(const MessageCapture&) = delete;
    MessageCapture& operator=(const MessageCapture&) = delete;

    size_t count(const char* id) const {
        return static_cast<size_t>(std::count(ids_.begin(), ids_.end(),
                                              CMString(id)));
    }

private:
    CMVector<CMString> ids_;
};

// 通配展开产物的 (实例 local id, pin id, flags 集合) 列表——instances_
// 为无序容器，展开序不锁死，逐项断言按排序比对
struct WildcardConnKey {
    uint64_t inst;
    uint64_t pin;
    unsigned flags;
    bool operator<(const WildcardConnKey& o) const {
        return std::tie(inst, pin, flags) <
               std::tie(o.inst, o.pin, o.flags);
    }
    bool operator==(const WildcardConnKey& o) const {
        return inst == o.inst && pin == o.pin && flags == o.flags;
    }
};

CMVector<WildcardConnKey> sorted_keys(
    const DSConnList<DSNetConnection>& conns) {
    CMVector<WildcardConnKey> keys;
    for (const DSNetConnection& c : conns) {
        keys.push_back({c.instance_local_id_.value(), c.pin_id_.value(),
                        c.flags_});
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

// 全部实例都有该引脚：每实例一条连接，标志位与显式写法逐位一致（电源
// 引脚 power 位 + INOUT hybrid、地引脚 ground + receiver、输出引脚
// driver——方向/类型均按单元定义）
TEST(DSNetWildcardConnectionTest, ExpandsToEveryInstanceOwningThePin) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.is_special = true;
    ctx.is_pg = true;  // 适配层派生口径 = special ∨ USE POWER/GROUND
    ctx.connections.push_back({"*", "VDD"});
    ctx.connections.push_back({"*", "VSS"});
    ctx.connections.push_back({"*", "ZN"});

    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{2});
    // u1/u2 INV_X1 有 VDD/VSS/ZN；u3 BUF_X1 无电源地、Z≠ZN → 各 2 条
    ASSERT_EQ(conns->size(), 6u);
    // VDD（pin 2）：INOUT POWER → hybrid + power；两个 INV 实例
    size_t vdd_seen = 0;
    size_t vss_seen = 0;
    size_t zn_seen = 0;
    for (const DSNetConnection& c : *conns) {
        EXPECT_TRUE(c.instance_local_id_ == CMInstanceId{1} ||
                    c.instance_local_id_ == CMInstanceId{2});
        if (c.pin_id_ == CMPinId{2}) {
            ++vdd_seen;
            EXPECT_TRUE(c.is_power());
            EXPECT_TRUE(c.is_driver() && c.is_receiver());  // hybrid
            EXPECT_FALSE(c.is_ground() || c.is_clock() || c.is_port());
        } else if (c.pin_id_ == CMPinId{3}) {
            ++vss_seen;
            EXPECT_TRUE(c.is_ground() && c.is_receiver());
            EXPECT_FALSE(c.is_power() || c.is_driver() || c.is_port());
        } else if (c.pin_id_ == CMPinId{1}) {
            ++zn_seen;
            EXPECT_TRUE(c.is_driver());
            EXPECT_FALSE(c.is_receiver() || c.is_power() || c.is_port());
        }
    }
    EXPECT_EQ(vdd_seen, 2u);
    EXPECT_EQ(vss_seen, 2u);
    EXPECT_EQ(zn_seen, 2u);
    EXPECT_EQ(net_data.stats_.connection_count, 6u);
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 0u);
    // 通配统计：3 个通配条目 → 6 条展开连接
    EXPECT_EQ(net_data.stats_.wildcard_entry_count, 3u);
    EXPECT_EQ(net_data.stats_.wildcard_connection_count, 6u);
    // pg 网判定与 USE 收录路径不受通配影响
    EXPECT_TRUE(net_data.is_pg_net(CMNetId{2}));
}

// 混合单元：只有拥有该引脚的单元的实例展开（u3 BUF_X1 无电源地引脚 →
// 不连）；信号引脚跨单元共享全局 id（A = id 0，裁定 3）时按「谁有谁连」
// 全量展开
TEST(DSNetWildcardConnectionTest, MixedCellsExpandOnlyOwningInstances) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"*", "VDD"});  // 仅 u1/u2（INV）有
    ctx.connections.push_back({"*", "A"});    // INV/BUF 均有（共享 id 0）

    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{1});
    // VDD 2 条（u1/u2）+ A 3 条（u1/u2/u3）
    ASSERT_EQ(conns->size(), 5u);
    const auto keys = sorted_keys(*conns);
    CMVector<WildcardConnKey> expect;
    for (const uint64_t inst : {1u, 2u}) {
        expect.push_back({inst, 2, 0});  // VDD：hybrid+power 位在下方单独核
    }
    for (const uint64_t inst : {1u, 2u, 3u}) {
        expect.push_back({inst, 0, 0});  // A INPUT → receiver 位
    }
    std::sort(expect.begin(), expect.end());
    // pin/实例配对全对上（flags 位形态已在上例锁定，此处核 id 配对）
    for (size_t i = 0; i < keys.size(); ++i) {
        EXPECT_EQ(keys[i].inst, expect[i].inst);
        EXPECT_EQ(keys[i].pin, expect[i].pin);
    }
    // A 条目 receiver 位（INPUT）；含 BUF 的 u3
    for (const DSNetConnection& c : *conns) {
        if (c.pin_id_ == CMPinId{0}) {
            EXPECT_TRUE(c.is_receiver());
            EXPECT_FALSE(c.is_driver());
        }
    }
    EXPECT_EQ(net_data.stats_.wildcard_entry_count, 2u);
    EXPECT_EQ(net_data.stats_.wildcard_connection_count, 5u);
}

// 引脚名全局未登记：零条连接、DSGN::0025 不触发（只计通配条目数）
TEST(DSNetWildcardConnectionTest, UnregisteredPinSilentZeroConnections) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSNetBuildData net_data;

    MessageCapture capture("DSGN::0025");
    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"*", "GHOST_PIN"});
    DSNetConnectionParseNode node;
    node.handle(ctx);

    EXPECT_TRUE(net_data.connections_.find(CMNetId{1}) ==
                net_data.connections_.end());
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 0u);
    EXPECT_EQ(capture.count("DSGN::0025"), 0u);
    EXPECT_EQ(net_data.stats_.wildcard_entry_count, 1u);
    EXPECT_EQ(net_data.stats_.wildcard_connection_count, 0u);
}

// 引脚全局已登记但没有任何实例拥有：零条连接、不报警
TEST(DSNetWildcardConnectionTest, PinUnownedByAnyCellSilent) {
    TestEnv env;
    env.design.register_pin("LONELY");  // 全局登记、无单元定义（id 8）
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSNetBuildData net_data;

    MessageCapture capture("DSGN::0025");
    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.connections.push_back({"*", "LONELY"});
    DSNetConnectionParseNode node;
    node.handle(ctx);

    EXPECT_TRUE(net_data.connections_.find(CMNetId{1}) ==
                net_data.connections_.end());
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 0u);
    EXPECT_EQ(capture.count("DSGN::0025"), 0u);
    EXPECT_EQ(net_data.stats_.wildcard_entry_count, 1u);
}

// 普通信号网（非 special、非 pg）上的通配同样展开（语法层处理，不按
// 网的类别特判）
TEST(DSNetWildcardConnectionTest, SignalNetWildcardExpandsToo) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "n1");
    ctx.is_special = false;
    ctx.connections.push_back({"*", "ZN"});
    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{1});
    ASSERT_EQ(conns->size(), 2u);  // u1/u2 的 ZN
    EXPECT_FALSE(net_data.is_pg_net(CMNetId{1}));
    EXPECT_EQ(net_data.stats_.wildcard_connection_count, 2u);
    EXPECT_EQ(net_data.stats_.connection_count, 2u);
}

// 通配 + 显式混写：同一 (实例, 引脚) 只保留一条——显式在前与在后两种
// 顺序都只出现一份显式条目（2026-11-24 连接表唯一性裁定：展开不再做
// 集合预判——通配对所有拥有者照常展开，重复由封存统一去掉；
// wildcard_connection_count = 封存前的展开条目数）
TEST(DSNetWildcardConnectionTest, ExplicitWinsNoDuplicate) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSNetBuildData net_data;

    // 显式在前
    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.connections.push_back({"u1", "VDD"});
    ctx.connections.push_back({"*", "VDD"});
    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();
    const auto conns = net_data.connections_.at(CMNetId{2});
    ASSERT_EQ(conns->size(), 2u);  // u1（显式 + 通配重复去重）+ u2（展开）
    size_t u1_vdd = 0;
    for (const DSNetConnection& c : *conns) {
        if (c.instance_local_id_ == CMInstanceId{1}) ++u1_vdd;
    }
    EXPECT_EQ(u1_vdd, 1u);
    EXPECT_EQ(net_data.stats_.connection_count, 2u);
    EXPECT_EQ(net_data.stats_.wildcard_entry_count, 1u);
    EXPECT_EQ(net_data.stats_.wildcard_connection_count, 2u);  // 封存前
    EXPECT_EQ(net_data.stats_.duplicate_connection_count, 1u);

    // 通配在前、显式在后（唯一性由封存保证，与顺序无关）
    DSNetBuildData net_data2;
    DSNetContext ctx2 = make_ctx(env, block_data, net_data2, "VDD");
    ctx2.connections.push_back({"*", "VDD"});
    ctx2.connections.push_back({"u1", "VDD"});
    node.handle(ctx2);
    net_data2.seal_all_connections();
    const auto conns2 = net_data2.connections_.at(CMNetId{2});
    ASSERT_EQ(conns2->size(), 2u);
    size_t u1_vdd2 = 0;
    for (const DSNetConnection& c : *conns2) {
        if (c.instance_local_id_ == CMInstanceId{1}) ++u1_vdd2;
    }
    EXPECT_EQ(u1_vdd2, 1u);
    EXPECT_EQ(net_data2.stats_.connection_count, 2u);
    EXPECT_EQ(net_data2.stats_.wildcard_connection_count, 2u);
    EXPECT_EQ(net_data2.stats_.duplicate_connection_count, 1u);
}

// 同一张网写两条同引脚通配（2026-11-24 评审修订 + 连接表唯一性裁定，
// 上游实证：连接按集合处理——OpenROAD 对 `( * VDD )` 写两条的变体日志
// 记 1504 条语句、网上的实例引脚连接仍 752 条）：两条通配各自展开（封
// 存前 4 条），封存去重后每实例仍只有一条连接；duplicate_connection_
// count = 封存时去掉的重复条数
TEST(DSNetWildcardConnectionTest, DuplicateWildcardEntriesDeduplicated) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSNetBuildData net_data;

    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.connections.push_back({"*", "VDD"});
    ctx.connections.push_back({"*", "VDD"});
    DSNetConnectionParseNode node;
    node.handle(ctx);
    const uint64_t removed = net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{2});
    ASSERT_EQ(conns->size(), 2u);  // u1/u2 各一条
    size_t u1_vdd = 0;
    size_t u2_vdd = 0;
    for (const DSNetConnection& c : *conns) {
        if (c.instance_local_id_ == CMInstanceId{1}) ++u1_vdd;
        if (c.instance_local_id_ == CMInstanceId{2}) ++u2_vdd;
    }
    EXPECT_EQ(u1_vdd, 1u);
    EXPECT_EQ(u2_vdd, 1u);
    EXPECT_EQ(net_data.stats_.connection_count, 2u);
    EXPECT_EQ(net_data.stats_.wildcard_entry_count, 2u);
    EXPECT_EQ(net_data.stats_.wildcard_connection_count, 4u);  // 封存前
    EXPECT_EQ(net_data.stats_.duplicate_connection_count, 2u);
    EXPECT_EQ(removed, 2u);
}

// 含伪单元实例（cell 引用快照表外）：通配路径静默跳过（与显式路径的
// DSGN::0025 口径不同——通配的本义是「谁有谁连」，无法判定就不连），
// 其余实例照常展开
TEST(DSNetWildcardConnectionTest, FakeCellInstanceSkippedSilently) {
    TestEnv env;
    DSBlockBuildData block_data = env.make_multi_block_data();
    DSInstance ghost;
    ghost.set_cell_id(CMCellId{9999});  // 快照表外 = 伪单元引用
    block_data.add_instance(std::move(ghost), "ghost");  // local 4
    DSNetBuildData net_data;

    MessageCapture capture("DSGN::0025");
    DSNetContext ctx = make_ctx(env, block_data, net_data, "VDD");
    ctx.connections.push_back({"*", "VDD"});
    DSNetConnectionParseNode node;
    node.handle(ctx);
    net_data.seal_all_connections();

    const auto conns = net_data.connections_.at(CMNetId{2});
    ASSERT_EQ(conns->size(), 2u);  // u1/u2 展开、ghost 跳过
    EXPECT_EQ(net_data.stats_.skipped_invalid_connection_count, 0u);
    EXPECT_EQ(capture.count("DSGN::0025"), 0u);
    EXPECT_EQ(net_data.stats_.wildcard_connection_count, 2u);
}

// 两种写法等价：通配 DEF 与逐实例显式 DEF 的连接表逐项相同（同 id、同
// 标志位；展开序随 instances_ 容器序与文本序可能不同——逐项比对按排序
// 键，与「同结构同标志位」的裁定语义一致）
TEST(DsDefWildcardEquivTest, WildcardDefEqualsExplicitDef) {
    TestEnv env;
    // 临时 DEF 落项目约定的临时目录设施（ScopedTempDir：RAII 构造即建、
    // 析构递归删——bazel 沙箱 TEST_TMPDIR，手动直跑落 .work/gtest_tmp/）
    test::ScopedTempDir tmp("wildcard_equiv");
    const std::string common_head =
        "VERSION 5.8 ;\n"
        "DESIGN nets_blk ;\n"
        "UNITS DISTANCE MICRONS 1000 ;\n"
        "COMPONENTS 3 ;\n"
        "    - u1 INV_X1 + PLACED ( 100 100 ) N ;\n"
        "    - u2 INV_X1 + PLACED ( 100 500 ) N ;\n"
        "    - u3 BUF_X1 + PLACED ( 100 900 ) N ;\n"
        "END COMPONENTS\n";
    const std::string common_tail =
        "END DESIGN\n";

    const fs::path wildcard_def = fs::path(tmp.path) / "wildcard_a.def";
    {
        std::ofstream f(wildcard_def);
        f << common_head
          << "SPECIALNETS 1 ;\n"
          << "    - VDD ( * VDD ) + USE POWER ;\n"
          << "END SPECIALNETS\n" << common_tail;
    }
    const fs::path explicit_def = fs::path(tmp.path) / "explicit_b.def";
    {
        std::ofstream f(explicit_def);
        f << common_head
          << "SPECIALNETS 1 ;\n"
          << "    - VDD ( u1 VDD ) ( u2 VDD ) + USE POWER ;\n"
          << "END SPECIALNETS\n" << common_tail;
    }

    const auto parse_one = [&](const fs::path& path) {
        DSBlockBuildData block_data;
        CMVector<DSCell> fake_cells;
        DSDefComponentsStats comp_stats;
        ds_parse_def_components(path.string(), test::borrow(env.stack),
                                test::borrow(env.design),
                                test::borrow(block_data), comp_stats, 0,
                                test::borrow(fake_cells));
        DSNetBuildData net_data;
        DSDefNetsStats net_stats;
        ds_parse_def_nets(path.string(), test::borrow(env.stack),
                          test::borrow(env.design),
                          test::borrow(block_data),
                          test::borrow(net_data), net_stats, 0, 1000);
        return net_data;
    };

    const DSNetBuildData from_wildcard = parse_one(wildcard_def);
    const DSNetBuildData from_explicit = parse_one(explicit_def);

    // 两边 VDD = local id 1（唯一网名），展开/显式连接表逐项相同
    const CMNetId vdd_id{1};
    const auto conns_w = from_wildcard.connections_.at(vdd_id);
    const auto conns_e = from_explicit.connections_.at(vdd_id);
    ASSERT_EQ(conns_w->size(), 2u);
    ASSERT_EQ(conns_e->size(), 2u);
    const auto keys_w = sorted_keys(*conns_w);
    const auto keys_e = sorted_keys(*conns_e);
    ASSERT_EQ(keys_w.size(), keys_e.size());
    for (size_t i = 0; i < keys_w.size(); ++i) {
        EXPECT_TRUE(keys_w[i] == keys_e[i]);
    }
    // 统计面：通配版记通配条目/展开连接，显式版为零
    EXPECT_EQ(from_wildcard.stats_.wildcard_entry_count, 1u);
    EXPECT_EQ(from_wildcard.stats_.wildcard_connection_count, 2u);
    EXPECT_EQ(from_explicit.stats_.wildcard_entry_count, 0u);
    EXPECT_EQ(from_explicit.stats_.wildcard_connection_count, 0u);
    // USE POWER 收录与 pg 判定两侧一致
    EXPECT_EQ(from_wildcard.net_use_of(vdd_id), DSNetUse::POWER);
    EXPECT_EQ(from_explicit.net_use_of(vdd_id), DSNetUse::POWER);
    EXPECT_TRUE(from_wildcard.is_pg_net(vdd_id));
    EXPECT_TRUE(from_explicit.is_pg_net(vdd_id));
}

}  // namespace
