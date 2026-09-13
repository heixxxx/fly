// S9 flatten 展平 + 分区保存单测（方案 design-db-plan.md §3.2 S9 +
// 2026-09-13 裁定补记①-⑤）：
//   1. 归属：core 内 primary 恰一 / extend 副本非 primary / 半开区间边界
//      （切线 x=1000 上的点不入左区 core、入右区 core——[xl, xh)）；
//   2. via 图形挂 net + 放置点 primary；DEF OBS → net 0 + obs 位；
//   3. 非 pg net 全量补全（跨分区连接四条全在）；pg 不补全（仅本分区
//      instance 副本相关条目）；
//   4. is_crossing 标记；geometry extend 交叠副本（不裁剪）；
//   5. id 换算三类（instance = start + local；net = start + local − 1；
//      不换算 S7 root）——连接项 id + flags 六位直存（2026-09-13 裁定：
//      S5b 解析边界换算完成，S9 零名字查询零 pin 几何依赖）；
//   6. 合并：两 slice 同区 merge 幂等 + 序列化往返。
// 期望值全部按实现口径手工推导（分区/坐标常数见 FlattenEnv），锁定行为。
#include <emir/design/cpp/ds_flatten.h>
#include <emir/design/cpp/ds_types.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace {

using namespace fly;

constexpr int32_t kIntMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kIntMax = std::numeric_limits<int32_t>::max();

// —— 测试环境（手工合成树 + 分区表，同 ds_partition_test 模式）────────
// 层级：root(top) → child csub(sub)（放置 (1000,500)——切线 x=1000 上）。
// root inst [0,4)：local 1 = la@(100,100)、local 2 = lb@(900,100)、
//   local 3 = csub；net [0,2)：n_top(local 1 → global 0)、n_pg(local 2 →
//   global 1, pg)；via [0,0)。
// sub inst [4,6)：local 1 = u1（local (100,100) → 全局 (1100,600)）；
//   net [2,4)：n1(local 1 → global 2)、n2(local 2 → global 3, pg)；
//   via [0,1)。
// 分区 '2x1'：p0 core (0,0,1000,2000) extend (MIN,MIN,1140,MAX)、
//   p1 core (1000,0,2000,2000) extend (860,MIN,MAX,MAX)。
// 连接项构造辅助（id + flags 形态；port 位按需；flags 位构造时置位
// 验证 S9 直存）
DSNetConnection make_conn(uint64_t local, uint32_t pin, bool port,
                          bool receiver = false, bool driver = false,
                          bool power = false) {
    DSNetConnection c;
    c.instance_local_id_ = local;
    c.pin_id_ = pin;
    if (port) c.set_port();
    if (receiver) c.set_receiver();
    if (driver) c.set_driver();
    if (power) c.set_power();
    return c;
}

struct FlattenEnv {
    DSHierTree tree;
    DSDesign design;
    DSBlockBuildData top;
    DSBlockBuildData sub;
    DSNetBuildData top_nets;
    DSNetBuildData sub_nets;
    CMVector<DSSubPartition> parts;

    FlattenEnv() {
        // 层表：M1(0)/VIA1(1)/M2(2)
        DSStack stack;
        DSLayer m1;
        m1.name_ = "M1";
        stack.add_layer(std::move(m1));
        DSLayer via1;
        via1.name_ = "VIA1";
        stack.add_layer(std::move(via1));
        DSLayer m2;
        m2.name_ = "M2";
        stack.add_layer(std::move(m2));
        (void)stack;

        // INV cell（id 0）：结构占位（连接 pin id 为合成值，S9 不查 cell）
        DSCell inv;
        inv.set_name("INV");
        design.add_cell(std::move(inv));

        // sub block cell（id 1，无 pin）
        DSCell sub_cell;
        sub_cell.set_name("sub");
        design.add_cell(std::move(sub_cell));

        // via cell（id 0）：cut ±20 @ VIA1 / bottom ±40 @ M1 / top ±50 @ M2
        DSViaCell via;
        via.set_name("V12");
        via.set_bottom_layer_id(0);
        via.set_top_layer_id(2);
        via.set_cut_layer_id(1);
        via.add_cut_rect(GEORect(-20, -20, 20, 20));
        via.add_bottom_enclosure(GEORect(-40, -40, 40, 40));
        via.add_top_enclosure(GEORect(-50, -50, 50, 50));
        design.add_via_cell(std::move(via));

        // 分区表（'2x1' 手工形态）
        DSSubPartition p0;
        p0.partition_id_ = 0;
        p0.xp_ = 0;
        p0.yp_ = 0;
        p0.core_rect_ = GEORect(0, 0, 1000, 2000);
        p0.extend_rect_ = GEORect(kIntMin, kIntMin, 1140, kIntMax);
        DSSubPartition p1;
        p1.partition_id_ = 1;
        p1.xp_ = 1;
        p1.yp_ = 0;
        p1.core_rect_ = GEORect(1000, 0, 2000, 2000);
        p1.extend_rect_ = GEORect(860, kIntMin, kIntMax, kIntMax);
        parts.push_back(p0);
        parts.push_back(p1);

        // 树：root(top) → child csub(sub)，复合变换随节点（S6 递推的
        // 手工等价：root 恒等、csub = translate(1000,500)）
        DSHierNode root;
        root.id_ = 0;
        root.parent_id_ = 0;
        root.block_cell_name_ = "top";
        root.instance_name_ = "top";
        root.self_global_id_ = 0;
        root.instance_start_ = 0;
        root.instance_count_ = 4;
        root.net_start_ = 0;
        root.net_count_ = 2;
        root.via_start_ = 0;
        root.via_count_ = 0;
        tree.nodes_.push_back(root);
        DSHierNode child;
        child.id_ = 1;
        child.parent_id_ = 0;
        child.block_cell_name_ = "sub";
        child.instance_name_ = "csub";
        child.self_global_id_ = 3;
        child.composite_transform_ =
            GEOTransform(GEOPoint(1000, 500), GEOOrientation::N);
        child.instance_start_ = 4;
        child.instance_count_ = 2;
        child.net_start_ = 2;
        child.net_count_ = 2;
        child.via_start_ = 0;
        child.via_count_ = 1;
        tree.nodes_.push_back(child);
        tree.nodes_[0].get_ref_children_ids().push_back(1);

        // top 定义：占位 + la/lb/csub（csub 在切线 x=1000 上）
        top.init_placeholder("top", DSDesign::kInvalidId);
        DSInstance la;
        la.cell_id_ = 0;
        la.transform_ = GEOTransform(GEOPoint(100, 100), GEOOrientation::N);
        la.placement_status_ = static_cast<uint8_t>(DSPlacementStatus::PLACED);
        top.add_instance(std::move(la), "la");
        DSInstance lb;
        lb.cell_id_ = 0;
        lb.transform_ = GEOTransform(GEOPoint(900, 100), GEOOrientation::N);
        lb.placement_status_ = static_cast<uint8_t>(DSPlacementStatus::PLACED);
        top.add_instance(std::move(lb), "lb");
        DSInstance csub;
        csub.cell_id_ = 1;
        csub.transform_ = GEOTransform(GEOPoint(1000, 500), GEOOrientation::N);
        csub.placement_status_ = static_cast<uint8_t>(DSPlacementStatus::PLACED);
        top.add_instance(std::move(csub), "csub");

        // top nets：n_top(local 1, 4 连接跨两分区) + n_pg(local 2, pg)。
        // 合成 pin id：A=1(receiver)、hybrid VDD=2、csub port PIN_IN=10、
        // top port TOP=20/VDD_TOP=21（port 位 + flags 位与解析产物同构）
        top_nets.add_connection(1, make_conn(1, 1, false, true));
        top_nets.add_connection(1, make_conn(2, 1, false, true));
        top_nets.add_connection(1, make_conn(3, 10, false, true));
        top_nets.add_connection(1, make_conn(0, 20, true, true));
        DSNetWire wt;
        wt.layer_id_ = 0;
        wt.width_ = 40;
        wt.points_ = {GEOPoint(0, 1800), GEOPoint(2000, 1800)};
        top_nets.add_wire(1, std::move(wt));
        top_nets.add_connection(2, make_conn(1, 2, false, true, true, true));
        top_nets.add_connection(2, make_conn(2, 2, false, true, true, true));
        top_nets.add_connection(2, make_conn(0, 21, true, true, true, true));
        DSNetWire wp;
        wp.layer_id_ = 0;
        wp.width_ = 40;
        wp.points_ = {GEOPoint(0, 1500), GEOPoint(500, 1500)};
        top_nets.add_wire(2, std::move(wp));
        top_nets.mark_pg_net(2);

        // sub 定义：占位 + u1（全局 (1100,600)，p0 extend 内副本）
        sub.init_placeholder("sub", DSDesign::kInvalidId);
        DSInstance u1;
        u1.cell_id_ = 0;
        u1.transform_ = GEOTransform(GEOPoint(100, 100), GEOOrientation::N);
        u1.placement_status_ = static_cast<uint8_t>(DSPlacementStatus::PLACED);
        sub.add_instance(std::move(u1), "u1");

        // sub nets：n1(local 1, wire + via) + n2(local 2, pg 无几何)。
        // 合成 port pin id：PIN_IN=10(receiver)、PIN_OUT=11(driver)
        sub_nets.add_connection(1, make_conn(1, 1, false, true));
        sub_nets.add_connection(1, make_conn(0, 10, true, true));
        DSNetWire w1;
        w1.layer_id_ = 0;
        w1.width_ = 40;
        w1.points_ = {GEOPoint(100, 100), GEOPoint(500, 100)};
        sub_nets.add_wire(1, std::move(w1));
        DSViaInstance vi;
        vi.via_cell_id_ = 0;
        vi.pos_ = GEOPoint(500, 100);
        sub_nets.add_via_instance(1, std::move(vi));
        sub_nets.add_connection(2, make_conn(1, 2, false, false, true));
        sub_nets.add_connection(2, make_conn(0, 11, true, false, true));
        sub_nets.mark_pg_net(2);

        // sub obstruction（DEF BLOCKAGE 等价物；全局 (1010,510,1060,560)）
        sub.obstructions_.push_back(DSShapeRef{2, GEORect(10, 10, 60, 60)});

    }
};

// 产物检索（pid 未产出 = nullptr）
const DSPartitionProduct* product_of(
    const CMVector<std::pair<uint32_t, DSPartitionProduct>>& slices,
    uint32_t pid) {
    for (const auto& [p, product] : slices) {
        if (p == pid) {
            return &product;
        }
    }
    return nullptr;
}

// 实例副本检索（未产出 = nullptr）
const DSInstance* inst_of(const DSPartitionProduct& p, uint64_t gid) {
    auto it = p.instances_.items_.find(gid);
    return it == p.instances_.items_.end() ? nullptr : &it->second;
}

// ── 1. 归属：primary 恰一 + extend 副本 + 半开区间边界 ─────────────

TEST(DSFlattenTest, PrimaryAssignmentAndExtendCopies) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.top, env.top_nets, env.design,
                         env.parts);
    // 两个分区都有实例副本（top 定义只覆盖这两区）
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    // la@(100,100)：仅 p0（core 内 primary 恰一；p1 extend 不含）
    const DSInstance* la0 = inst_of(*p0, 1);
    ASSERT_NE(la0, nullptr);
    EXPECT_TRUE(la0->is_primary());
    EXPECT_EQ(la0->get_transform().get_offset().get_x(), 100);
    EXPECT_EQ(inst_of(*p1, 1), nullptr);

    // lb@(900,100)：p0 primary + p1 extend 副本（非 primary）
    const DSInstance* lb0 = inst_of(*p0, 2);
    const DSInstance* lb1 = inst_of(*p1, 2);
    ASSERT_NE(lb0, nullptr);
    ASSERT_NE(lb1, nullptr);
    EXPECT_TRUE(lb0->is_primary());
    EXPECT_FALSE(lb1->is_primary());

    // csub 在切线 x=1000 上：半开区间 [0,1000) 不含 → 非 p0 primary；
    // [1000,2000) 含 → p1 primary。p0 仍持 extend 副本（非 primary）。
    const DSInstance* csub0 = inst_of(*p0, 3);
    const DSInstance* csub1 = inst_of(*p1, 3);
    ASSERT_NE(csub0, nullptr);
    ASSERT_NE(csub1, nullptr);
    EXPECT_FALSE(csub0->is_primary());
    EXPECT_TRUE(csub1->is_primary());
    // 副本 transform = 全局坐标（⑧ local 0 自身由父块展开产出）
    EXPECT_EQ(csub1->get_transform().get_offset().get_x(), 1000);
    EXPECT_EQ(csub1->get_transform().get_offset().get_y(), 500);

    // 分区对象恰一 primary：p0 = {1,2,3}、p1 = {2,3}，无重复 id
    EXPECT_EQ(p0->instances_.size(), 3u);
    EXPECT_EQ(p1->instances_.size(), 2u);
}

TEST(DSFlattenTest, ChildDefInstancesUseCompositeTransform) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.sub, env.sub_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    // u1 global id = inst_start(4) + local(1) = 5；全局 pos = 复合 ×
    // local pos = (1100,600)：p1 core primary + p0 extend 副本
    const DSInstance* u10 = inst_of(*p0, 5);
    const DSInstance* u11 = inst_of(*p1, 5);
    ASSERT_NE(u10, nullptr);
    ASSERT_NE(u11, nullptr);
    EXPECT_FALSE(u10->is_primary());
    EXPECT_TRUE(u11->is_primary());
    EXPECT_EQ(u11->get_transform().get_offset().get_x(), 1100);
    EXPECT_EQ(u11->get_transform().get_offset().get_y(), 600);
    // local 0 占位不产出副本
    EXPECT_EQ(inst_of(*p1, 4), nullptr);
}

// ── 2. 几何副本：extend 交叠不裁剪 + is_crossing ────────────────────

TEST(DSFlattenTest, GeometryCopiesUncrossedAndCrossing) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.top, env.top_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    // n_top(global 0) wire (0,1800)-(2000,1800) w40 → 段矩形
    // (−20,1780,2020,1820)（宽度向两端/两侧各扩 half_w=20，同 S5b 密度
    // 节点口径）：两分区 extend 均交叠 → 副本两份、原矩形不裁剪
    const auto* e0 = p0->geometry_.entries_of(0);
    const auto* e1 = p1->geometry_.entries_of(0);
    ASSERT_NE(e0, nullptr);
    ASSERT_NE(e1, nullptr);
    int wire0 = 0;
    for (const auto& e : *e0) {
        if (!e.is_obs()) {
            ++wire0;
            EXPECT_EQ(e.layer_id_, 0u);
            EXPECT_EQ(e.rect_.get_x_low(), -20);
            EXPECT_EQ(e.rect_.get_y_low(), 1780);
            EXPECT_EQ(e.rect_.get_x_high(), 2020);
            EXPECT_EQ(e.rect_.get_y_high(), 1820);
        }
    }
    EXPECT_EQ(wire0, 1);
    ASSERT_EQ(e1->size(), 1u);
    EXPECT_FALSE((*e1)[0].is_obs());
    // 跨分区 → is_crossing 两侧标记
    EXPECT_TRUE(p0->geometry_.is_crossing(0));
    EXPECT_TRUE(p1->geometry_.is_crossing(0));

    // n_pg(global 1) wire (0,1500)-(500,1500)：仅 p0 extend 命中 → 单区
    // 副本、不标 crossing
    const auto* pg0 = p0->geometry_.entries_of(1);
    ASSERT_NE(pg0, nullptr);
    EXPECT_EQ(pg0->size(), 1u);
    EXPECT_EQ(p1->geometry_.entries_of(1), nullptr);
    EXPECT_FALSE(p0->geometry_.is_crossing(1));
    EXPECT_FALSE(p1->geometry_.is_crossing(1));
}

TEST(DSFlattenTest, ViaEntriesCarryCellIdAndPlacementPrimary) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.sub, env.sub_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p1, nullptr);

    // n1(global 2) via @ local (500,100) → 全局 (1500,600)：放置点在 p1
    // core → via 条目 primary 位仅 p1 置位；cut/enclosure 三组矩形按
    // via cell 定义展开、挂 via cell id
    const auto* entries = p1->geometry_.entries_of(2);
    ASSERT_NE(entries, nullptr);
    // wire 段 1 条（非 via）+ via 图形 3 条 = 4
    ASSERT_EQ(entries->size(), 4u);
    int wire_count = 0;
    int via_count = 0;
    for (const auto& e : *entries) {
        if (!e.is_via()) {
            ++wire_count;
            // wire (100,100)-(500,100) w40 × 复合 → 段矩形外扩 half_w
            EXPECT_EQ(e.rect_.get_x_low(), 1080);
            EXPECT_EQ(e.rect_.get_y_low(), 580);
            EXPECT_EQ(e.rect_.get_x_high(), 1520);
            EXPECT_EQ(e.rect_.get_y_high(), 620);
            continue;
        }
        ++via_count;
        EXPECT_EQ(e.via_cell_id_, 0u);
        EXPECT_TRUE(e.is_primary());  // 放置点 (1500,600) ∈ p1 core
    }
    EXPECT_EQ(wire_count, 1);
    EXPECT_EQ(via_count, 3);
    // cut ±20 → (1480,580,1520,620)；bottom ±40 → (1460,560,1540,640)；
    // top ±50 → (1450,550,1550,650)（含层归属）
    const auto expect_rect = [&](int layer, int32_t xl, int32_t yl,
                                 int32_t xh, int32_t yh) {
        for (const auto& e : *entries) {
            if (e.is_via() && e.layer_id_ == static_cast<uint32_t>(layer) &&
                e.rect_.get_x_low() == xl && e.rect_.get_y_low() == yl) {
                EXPECT_EQ(e.rect_.get_x_high(), xh);
                EXPECT_EQ(e.rect_.get_y_high(), yh);
                return;
            }
        }
        FAIL() << "via rect not found: layer " << layer;
    };
    expect_rect(1, 1480, 580, 1520, 620);  // cut @ VIA1
    expect_rect(0, 1460, 560, 1540, 640);  // bottom @ M1
    expect_rect(2, 1450, 550, 1550, 650);  // top @ M2
    // via 图形越出 p0 extend（x ≥ 1450 > 1140）→ p0 无 via 条目
    const DSPartitionProduct* p0 = product_of(slices, 0);
    ASSERT_NE(p0, nullptr);
    const auto* e0 = p0->geometry_.entries_of(2);
    ASSERT_NE(e0, nullptr);
    for (const auto& e : *e0) {
        EXPECT_FALSE(e.is_via());
    }
}

TEST(DSFlattenTest, ObstructionGoesToNetZeroBucketWithObsFlag) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.sub, env.sub_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    // obstruction (10,10,60,60) × 复合 translate(1000,500) →
    // (1010,510,1060,560)：两分区 extend 均命中 → net 0 桶 + obs 位
    //（root 首网 global 0 与 OBS 桶同键共存——obs 位判别）
    for (const DSPartitionProduct* p : {p0, p1}) {
        const auto* entries = p->geometry_.entries_of(0);
        ASSERT_NE(entries, nullptr) << "partition " << p->instances_.size();
        int obs_count = 0;
        for (const auto& e : *entries) {
            if (e.is_obs()) {
                ++obs_count;
                EXPECT_EQ(e.layer_id_, 2u);
                EXPECT_EQ(e.rect_.get_x_low(), 1010);
                EXPECT_EQ(e.rect_.get_y_low(), 510);
                EXPECT_EQ(e.rect_.get_x_high(), 1060);
                EXPECT_EQ(e.rect_.get_y_high(), 560);
            }
        }
        EXPECT_EQ(obs_count, 1);
    }
}

// ── 3. 连接：非 pg 全量补全 / pg 仅本区 instance 副本条目 ───────────

TEST(DSFlattenTest, NonPgNetConnectionsFullyCompleted) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.top, env.top_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    // n_top(global 0, 非 pg) 四条连接（la/lb/csub/PIN）跨分区也全量保
    // 存——两分区各一份完整列表（pin id + flags 位直存）
    for (const DSPartitionProduct* p : {p0, p1}) {
        auto it = p->net_connections_.items_.find(0);
        ASSERT_NE(it, p->net_connections_.items_.end());
        ASSERT_EQ(it->second.size(), 4u);
        EXPECT_EQ(it->second[0].instance_global_id_, 1u);
        EXPECT_EQ(it->second[0].pin_id_, 1u);
        EXPECT_FALSE(it->second[0].is_port());
        EXPECT_TRUE(it->second[0].is_receiver());
        EXPECT_EQ(it->second[1].instance_global_id_, 2u);
        // csub 端点 = 块实例 global id 3；PIN 端点 = root 自身 global id
        // 0 + port 位（⑧ local 0 映射）；flags 位整体直存
        EXPECT_EQ(it->second[2].instance_global_id_, 3u);
        EXPECT_EQ(it->second[2].pin_id_, 10u);
        EXPECT_FALSE(it->second[2].is_port());
        EXPECT_TRUE(it->second[2].is_receiver());
        EXPECT_EQ(it->second[3].instance_global_id_, 0u);
        EXPECT_EQ(it->second[3].pin_id_, 20u);
        EXPECT_TRUE(it->second[3].is_port());
        EXPECT_TRUE(it->second[3].is_receiver());
    }
}

TEST(DSFlattenTest, PgNetConnectionsFilteredToLocalInstances) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.top, env.top_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    // n_pg(global 1, pg)：几何仅 p0 → 仅 p0 有 NET_CONNECTIONS，且按本
    // 区 instance 副本过滤：la ✓（p0 副本）、lb ✓（p0 副本）、port（root
    // 自身副本 p0）✓；p1 无该网条目（且 n2 无几何 → 全域无条目）
    auto it0 = p0->net_connections_.items_.find(1);
    ASSERT_NE(it0, p0->net_connections_.items_.end());
    EXPECT_EQ(it0->second.size(), 3u);
    EXPECT_EQ(p1->net_connections_.items_.count(1), 0u);
    EXPECT_EQ(p0->net_connections_.items_.count(3), 0u);
    EXPECT_EQ(p1->net_connections_.items_.count(3), 0u);
}

TEST(DSFlattenTest, InstConnectionsFollowCopies) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.sub, env.sub_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);

    // u1(global 5) 两分区副本各带自身连接端点：n1(global 2) pin 1
    // (receiver) + n2(global 3) pin 2 (driver)（n2 为 pg 无几何——
    // instance 维度仍可达，拼装口径）。两端点来自不同网（conn_bucket 按
    // 连接表 unordered 键序累积），按 (net, pin) 集合断言，不依赖桶序。
    const auto expect_conn_set = [](const CMVector<DSPartConnection>& conns,
                                    std::pair<uint64_t, uint32_t> a,
                                    std::pair<uint64_t, uint32_t> b) {
        ASSERT_EQ(conns.size(), 2u);
        for (const auto& c : conns) {
            if (c.net_global_id_ == a.first) {
                EXPECT_EQ(c.pin_id_, a.second);
            } else if (c.net_global_id_ == b.first) {
                EXPECT_EQ(c.pin_id_, b.second);
            } else {
                FAIL() << "unexpected net " << c.net_global_id_;
            }
        }
    };
    for (const DSPartitionProduct* p : {p0, p1}) {
        auto it = p->inst_connections_.items_.find(5);
        ASSERT_NE(it, p->inst_connections_.items_.end());
        expect_conn_set(it->second, {2, 1}, {3, 2});
    }
    // 块自身 port 引用（local 0 条目 PIN_IN/PIN_OUT）→ 挂 csub global
    // id 3（父块展开产出的副本位置——本测试只展开 sub，parent 视角由
    // top 展开补）；位直存（PIN_IN receiver / PIN_OUT driver）
    auto it = p1->inst_connections_.items_.find(3);
    ASSERT_NE(it, p1->inst_connections_.items_.end());
    ASSERT_EQ(it->second.size(), 2u);
    for (const auto& c : it->second) {
        EXPECT_TRUE(c.is_port());
        if (c.net_global_id_ == 2u) {
            EXPECT_EQ(c.pin_id_, 10u);
            EXPECT_TRUE(c.is_receiver());
        } else {
            EXPECT_EQ(c.net_global_id_, 3u);
            EXPECT_EQ(c.pin_id_, 11u);
            EXPECT_TRUE(c.is_driver());
        }
    }
}

// ── 4. 合并幂等 + 序列化往返 ────────────────────────────────────────

TEST(DSFlattenTest, MergeIsIdempotentForSameSlice) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.sub, env.sub_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p1, nullptr);
    const size_t inst_count = p1->instances_.size();
    const size_t geom_count = p1->geometry_.nets_.size();

    DSPartitionProduct merged;
    merged.merge_from(*p1);
    merged.merge_from(*p1);  // 同 slice 重放：instance 键覆盖、不翻倍
    EXPECT_EQ(merged.instances_.size(), inst_count);
    EXPECT_EQ(merged.geometry_.nets_.size(), geom_count);
    // 连接列表拼接（同源重放会重复条目——重放仅发生在同任务重投语义，
    // 正常编排每 slice 只合并一次；此处锁定 merge 的追加语义）
    EXPECT_EQ(merged.inst_connections_.items_.at(5).size(),
              2 * p1->inst_connections_.items_.at(5).size());
}

TEST(DSFlattenTest, ProductSerializeRoundTrip) {
    FlattenEnv env;
    const auto slices =
        ds_flatten_block(env.tree, env.sub, env.sub_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p1, nullptr);

    CMString blob;
    FLY_ENCODE(*p1, blob);
    DSPartitionProduct back;
    FLY_DECODE(blob, DSPartitionProduct, back);

    EXPECT_EQ(back.instances_.size(), p1->instances_.size());
    const DSInstance* u1 = inst_of(back, 5);
    ASSERT_NE(u1, nullptr);
    EXPECT_TRUE(u1->is_primary());
    // geometry：net 0（OBS 桶）与 net 2（wire+via）共存
    EXPECT_EQ(back.geometry_.nets_.size(), p1->geometry_.nets_.size());
    ASSERT_NE(back.geometry_.entries_of(2), nullptr);
    EXPECT_EQ(back.geometry_.entries_of(2)->size(), 4u);
    EXPECT_TRUE(back.geometry_.is_crossing(2));
    // 连接表往返
    ASSERT_NE(back.inst_connections_.items_.find(5),
              back.inst_connections_.items_.end());
    EXPECT_EQ(back.inst_connections_.items_[5].size(), 2u);
    ASSERT_NE(back.net_connections_.items_.find(2),
              back.net_connections_.items_.end());
    EXPECT_EQ(back.net_connections_.items_[2].size(), 2u);
}

TEST(DSFlattenTest, OutOfCorePointFallsBackToNearestPrimary) {
    // review 2026-09-13 补测：放置点越出全部 core（core x ∈ [0,2000)）
    // 的防御回退——恰一 primary 不变式的组成部分：取距 core 最近的分区
    //（x=3000 距 p1 右界 2000 为 1000、距 p0 为 3000 → p1），副本集含
    // primary（x=3000 亦在 p1 extend [860,INT_MAX] 内；p0 extend 上界
    // 1140 不含 → 无 p0 副本）
    FlattenEnv env;
    DSInstance far_i;
    far_i.cell_id_ = 0;
    far_i.transform_ = GEOTransform(GEOPoint(3000, 100), GEOOrientation::N);
    far_i.placement_status_ =
        static_cast<uint8_t>(DSPlacementStatus::PLACED);
    env.top.add_instance(std::move(far_i), "far");

    const auto slices =
        ds_flatten_block(env.tree, env.top, env.top_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    const DSPartitionProduct* p1 = product_of(slices, 1);
    ASSERT_NE(p0, nullptr);
    ASSERT_NE(p1, nullptr);
    // far local id 4（占位+la/lb/csub 之后）→ global = inst_start 0 + 4
    ASSERT_EQ(p1->instances_.items_.count(4u), 1u);
    EXPECT_TRUE(p1->instances_.items_.at(4u).is_primary());
    EXPECT_EQ(p0->instances_.items_.count(4u), 0u);
    // 全表恰一 primary 不变式仍成立（含回退目标）
    int primary_total = 0;
    for (const auto& [id, inst] : p1->instances_.items_) {
        (void)id;
        primary_total += inst.is_primary() ? 1 : 0;
    }
    for (const auto& [id, inst] : p0->instances_.items_) {
        (void)id;
        primary_total += inst.is_primary() ? 1 : 0;
    }
    EXPECT_EQ(primary_total, 4);  // la/lb/csub(切线归 p1)+far 各恰一
}

TEST(DSFlattenTest, RootFirstNetAndObsShareNetZeroBucket) {
    // review 2026-09-13 补测（单测锁定，QA 外的独立防线）：root 首网
    //（n_top local 1 → global 0）的真实几何与 DEF OBS 在 net 0 桶共存，
    // obs 位判别 + net_entries/obs_entries 过滤视图正确
    FlattenEnv env;
    env.top.obstructions_.push_back(
        DSShapeRef{2, GEORect(10, 10, 60, 60)});  // M2 层局部矩形

    const auto slices =
        ds_flatten_block(env.tree, env.top, env.top_nets, env.design,
                         env.parts);
    const DSPartitionProduct* p0 = product_of(slices, 0);
    ASSERT_NE(p0, nullptr);
    const auto* bucket = p0->geometry_.entries_of(0);
    ASSERT_NE(bucket, nullptr);
    // 共存：n_top wire（非 obs）+ OBS 条目各 1
    int wire_count = 0, obs_count = 0;
    for (const auto& e : *bucket) {
        (e.is_obs() ? obs_count : wire_count) += 1;
    }
    EXPECT_EQ(wire_count, 1);
    EXPECT_EQ(obs_count, 1);
    // 过滤视图：net_entries(0) 只含真实网几何；obs_entries 只含 OBS
    EXPECT_EQ(p0->geometry_.net_entries(0).size(), 1u);
    EXPECT_FALSE(p0->geometry_.net_entries(0)[0].is_obs());
    EXPECT_EQ(p0->geometry_.obs_entries().size(), 1u);
    EXPECT_TRUE(p0->geometry_.obs_entries()[0].is_obs());
    EXPECT_EQ(p0->geometry_.obs_entries()[0].layer_id_, 2u);
}

TEST(DSFlattenTest, ObstructionAndPartitionGeometryRoundTrip) {
    DSPartitionGeometry g;
    DSGeomEntry obs;
    obs.layer_id_ = 2;
    obs.rect_ = GEORect(1010, 510, 1060, 560);
    obs.set_obs();
    g.add_entry(0, std::move(obs));
    g.mark_crossing(0);
    DSGeomEntry via;
    via.layer_id_ = 1;
    via.rect_ = GEORect(1480, 580, 1520, 620);
    via.via_cell_id_ = 0;
    via.set_primary();
    g.add_entry(2, std::move(via));

    CMString blob;
    FLY_ENCODE(g, blob);
    DSPartitionGeometry back;
    FLY_DECODE(blob, DSPartitionGeometry, back);

    EXPECT_EQ(back.nets_.size(), 2u);
    ASSERT_NE(back.entries_of(0), nullptr);
    EXPECT_EQ(back.entries_of(0)->size(), 1u);
    EXPECT_TRUE(back.entries_of(0)->front().is_obs());
    ASSERT_NE(back.entries_of(2), nullptr);
    EXPECT_TRUE(back.entries_of(2)->front().is_via());
    EXPECT_TRUE(back.entries_of(2)->front().is_primary());
    EXPECT_TRUE(back.is_crossing(0));
    EXPECT_FALSE(back.is_crossing(2));
}

}  // namespace
