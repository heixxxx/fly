// S10 汇总校验 + 冻结单测（方案 design-db-plan.md §3.2 S10 + 2026-09-13
// 校验分级裁定：损坏类 fatal / 观测类 warn）：
//   1. 分区级校验：计数（primary/副本/几何条目/连接/跨分区网）+ 全局校验
//      素材 id 集提取（net 0 桶仅含 OBS 时不计网覆盖）；
//   2. 覆盖校验（损坏类）：完整网格过（含单分区 '1x1'）/ 缺一分区 / 行列
//      边界不一致 / 与全局格网覆盖域不对齐 → coverage_gap_ 非空；
//   3. 并查集自洽（损坏类）：两层 + 双向过 / root 非自映射 / members 与
//      root_of_ 不一致 → union_inconsistency_ 非空；
//   4. namemap 双向闭环（损坏类）：六 hasher 全查过 / 单向断裂 →
//      namemap_inconsistency_ 非空；
//   5. id 连续性（观测类）：空洞/重复计数正确、损坏类描述保持为空；
//   6. 密度守恒（观测类，primary 口径）：Σ 各分区 primary 计数 = Σ 首份
//      定义 (实例数 − UNPLACED) 过 / 偏差报出；
//   7. 报告序列化往返；
//   8. fatal 路径：fork 断言 rc=80（fly::test::expect_fatal_exit_code）。
// 期望值全部按实现口径手工推导（环境常数见 VerifyEnv 头注释），锁定行为。
#include <emir/design/cpp/ds_types.h>
#include <emir/design/cpp/ds_union.h>
#include <emir/design/cpp/ds_verify.h>

#include <common/testing/cpp/test_helpers.h>
#include <gtest/gtest.h>

#include <csignal>
#include <cstdint>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace {

using namespace fly;

// —— 测试环境（手工合成，同 ds_flatten_test 模式）────────────────────
// 树：root(top) → child csub(sub)。root inst [0,4) net [0,3) via [0,2)；
// child inst [4,6) net [3,5) via [2,3)。推导口径：
//   可用实例 id = Σ count − (非 root local 0 保留槽) − root 自身
//              = (4 + 2) − 1 − 1 = 4 → {1,2,3,5}（top1/top2/top3/u1）
//   net 期望域 = 3 + 2 = 5；via 期望域 = 2 + 1 = 3（top 两过孔 → {0,1}、
//   sub 一过孔 → {2}）
// 分区 '2x1'：p0 core (0,0,1000,2000)、p1 core (1000,0,2000,2000)；密度
// 格网 bin 1000、2×2 格 → 覆盖域 (0,0)-(2000,2000) 与 core 并集一致。
struct VerifyEnv {
    DSStack stack;
    DSDesign design;
    DSHierTree tree;
    DSBlockBuildData top;
    DSBlockBuildData sub;
    DSNetBuildData top_nets;
    DSNetBuildData sub_nets;
    DSBlockNames top_names;
    DSBlockNames sub_names;
    DSDensityGrid density;
    DSNetUnion net_union;
    CMVector<DSSubPartition> parts;

    VerifyEnv() {
        // 层表（layer hasher 随 add_layer 登记）：M1(0)/VIA1(1)
        DSLayer m1;
        m1.name_ = "M1";
        stack.add_layer(std::move(m1));
        DSLayer via1;
        via1.name_ = "VIA1";
        stack.add_layer(std::move(via1));

        // INV cell（id 0）+ VDD pin（pin hasher 组合键登记）
        DSCell inv;
        inv.set_name("INV");
        DSPin vdd;
        vdd.pin_id_ = 7;
        inv.add_pin(std::move(vdd));
        design.add_cell(std::move(inv));
        design.register_pin("INV", "VDD", 7);
        // sub block cell（id 1）+ via cell（id 0）
        DSCell sub_cell;
        sub_cell.set_name("sub");
        design.add_cell(std::move(sub_cell));
        DSViaCell via;
        via.set_name("V12");
        design.add_via_cell(std::move(via));

        // 分区表 '2x1' + 密度格网（覆盖域一致；计数手工放置——统计汇总
        // 断言用 inst=10 / metal=5 / via=7）
        DSSubPartition p0;
        p0.partition_id_ = 0;
        p0.xp_ = 0;
        p0.yp_ = 0;
        p0.core_rect_ = GEORect(0, 0, 1000, 2000);
        DSSubPartition p1;
        p1.partition_id_ = 1;
        p1.xp_ = 1;
        p1.yp_ = 0;
        p1.core_rect_ = GEORect(1000, 0, 2000, 2000);
        parts.push_back(p0);
        parts.push_back(p1);
        design.partitions_ = parts;
        density.configure(0, 0, 1000, 1000, 2, 2);
        density.counts_ = {1, 2, 3, 4};
        density.metal_layer_counts_[0] = {5};
        density.via_layer_counts_[1] = {7};

        // 树（root → child，区间手工合成）
        DSHierNode root;
        root.id_ = 0;
        root.parent_id_ = 0;
        root.block_cell_name_ = "top";
        root.instance_name_ = "top";
        root.self_global_id_ = 0;
        root.instance_start_ = 0;
        root.instance_count_ = 4;
        root.net_start_ = 0;
        root.net_count_ = 3;
        root.via_start_ = 0;
        root.via_count_ = 2;
        tree.nodes_.push_back(root);
        DSHierNode child;
        child.id_ = 1;
        child.parent_id_ = 0;
        child.block_cell_name_ = "sub";
        child.instance_name_ = "csub";
        child.self_global_id_ = 3;
        child.instance_start_ = 4;
        child.instance_count_ = 2;
        child.net_start_ = 3;
        child.net_count_ = 2;
        child.via_start_ = 2;
        child.via_count_ = 1;
        tree.nodes_.push_back(child);
        tree.nodes_[0].get_ref_children_ids().push_back(1);

        // per-DEF 产物（计数与树对齐；via 统计随 add_via_instance 回填）
        top.init_placeholder("top", DSDesign::kInvalidId);
        for (const char* n : {"la", "lb", "csub"}) {
            DSInstance inst;
            inst.set_cell_id(0);
            top.add_instance(std::move(inst), n);
        }
        top.stats_.instance_count = 3;
        top.register_net("t0");
        top.register_net("t1");
        top.register_net("t2");
        DSViaInstance tv1;
        tv1.via_cell_id_ = 0;
        tv1.pos_ = GEOPoint(10, 10);
        top_nets.add_via_instance(1, std::move(tv1));
        DSViaInstance tv2;
        tv2.via_cell_id_ = 0;
        tv2.pos_ = GEOPoint(20, 20);
        top_nets.add_via_instance(2, std::move(tv2));

        sub.init_placeholder("sub", DSDesign::kInvalidId);
        DSInstance u1;
        u1.set_cell_id(0);
        sub.add_instance(std::move(u1), "u1");
        sub.stats_.instance_count = 1;
        sub.register_net("s0");
        sub.register_net("s1");
        DSViaInstance sv;
        sv.via_cell_id_ = 0;
        sv.pos_ = GEOPoint(30, 30);
        sub_nets.add_via_instance(1, std::move(sv));

        // 名字伴生对象（hasher 与产物共享，flow attach 同构）
        top_names.block_name_ = "top";
        top_names.instance_names_ = top.instance_names_;
        top_names.net_names_ = top.net_names_;
        sub_names.block_name_ = "sub";
        sub_names.instance_names_ = sub.instance_names_;
        sub_names.net_names_ = sub.net_names_;

        // 并查集（两层规范化形态：root 自映射 + 成员反向索引）
        net_union.root_of_[10] = 10;
        net_union.root_of_[11] = 10;
        net_union.members_of_[10] = CMVector<uint64_t>{10, 11};
    }
};

// —— 分区产物（手工合成，primary 语义自洽；期望值见各用例断言）──────
// p0：inst {1 primary, 2 primary, 3 副本}；geometry {0: wire, 1: wire,
//     2: via(primary)} + crossing {0}；iconn {1: [net0, net1]}；
//     nconn {0: [2 条]}
// p1：inst {3 primary, 5 primary}；geometry {3: wire, 4: wire}；
//     iconn {5: [net3, net4]}；nconn {3: [1 条]}
struct PartitionPair {
    DSPartitionProduct p0;
    DSPartitionProduct p1;
};

PartitionPair make_products() {
    PartitionPair pp;
    DSPartConnection c1;
    c1.instance_global_id_ = 1;
    c1.net_global_id_ = 0;
    pp.p0.inst_connections_.items_[1].push_back(c1);
    DSPartConnection c2;
    c2.instance_global_id_ = 1;
    c2.net_global_id_ = 1;
    pp.p0.inst_connections_.items_[1].push_back(c2);
    DSPartConnection n1;
    n1.instance_global_id_ = 1;
    n1.net_global_id_ = 0;
    pp.p0.net_connections_.items_[0].push_back(n1);
    DSPartConnection n2;
    n2.instance_global_id_ = 2;
    n2.net_global_id_ = 0;
    pp.p0.net_connections_.items_[0].push_back(n2);
    DSGeomEntry g0;
    g0.layer_id_ = 0;
    g0.rect_ = GEORect(0, 100, 500, 140);
    pp.p0.geometry_.add_entry(0, std::move(g0));
    DSGeomEntry g1;
    g1.layer_id_ = 0;
    g1.rect_ = GEORect(0, 200, 300, 240);
    pp.p0.geometry_.add_entry(1, std::move(g1));
    DSGeomEntry g2;
    g2.layer_id_ = 1;
    g2.rect_ = GEORect(400, 300, 500, 400);
    g2.via_cell_id_ = 0;
    g2.set_primary();
    pp.p0.geometry_.add_entry(2, std::move(g2));
    pp.p0.geometry_.mark_crossing(0);
    DSInstance i1;
    i1.set_cell_id(0);
    i1.set_primary();
    pp.p0.instances_.items_[1] = std::move(i1);
    DSInstance i2;
    i2.set_cell_id(0);
    i2.set_primary();
    pp.p0.instances_.items_[2] = std::move(i2);
    DSInstance i3;
    i3.set_cell_id(1);
    pp.p0.instances_.items_[3] = std::move(i3);

    DSPartConnection c3;
    c3.instance_global_id_ = 5;
    c3.net_global_id_ = 3;
    pp.p1.inst_connections_.items_[5].push_back(c3);
    DSPartConnection c4;
    c4.instance_global_id_ = 5;
    c4.net_global_id_ = 4;
    pp.p1.inst_connections_.items_[5].push_back(c4);
    DSPartConnection n3;
    n3.instance_global_id_ = 5;
    n3.net_global_id_ = 3;
    pp.p1.net_connections_.items_[3].push_back(n3);
    DSGeomEntry g3;
    g3.layer_id_ = 0;
    g3.rect_ = GEORect(1100, 100, 1500, 140);
    pp.p1.geometry_.add_entry(3, std::move(g3));
    DSGeomEntry g4;
    g4.layer_id_ = 0;
    g4.rect_ = GEORect(1100, 200, 1400, 240);
    pp.p1.geometry_.add_entry(4, std::move(g4));
    DSInstance i3b;
    i3b.set_cell_id(1);
    i3b.set_primary();
    pp.p1.instances_.items_[3] = std::move(i3b);
    DSInstance i5;
    i5.set_cell_id(0);
    i5.set_primary();
    pp.p1.instances_.items_[5] = std::move(i5);
    return pp;
}

// 校验结果对（环境 + 产物 → 两分区校验结果；供全局校验各用例复用）
CMVector<DSPartitionCheckResult> check_products(const PartitionPair& pp) {
    CMVector<DSPartitionCheckResult> checks;
    checks.push_back(
        ds_verify_partition(0, 0, 0, pp.p0.geometry_, pp.p0.instances_,
                            pp.p0.inst_connections_, pp.p0.net_connections_));
    checks.push_back(
        ds_verify_partition(1, 1, 0, pp.p1.geometry_, pp.p1.instances_,
                            pp.p1.inst_connections_, pp.p1.net_connections_));
    return checks;
}

CMVector<const DSPartitionCheckResult*> check_ptrs(
    const CMVector<DSPartitionCheckResult>& checks) {
    CMVector<const DSPartitionCheckResult*> ptrs;
    for (const DSPartitionCheckResult& c : checks) {
        ptrs.push_back(&c);
    }
    return ptrs;
}

// 全局校验入参打包（环境 + 校验结果 → 报告）
DSDesignCheckReport verify_env_design(
    const VerifyEnv& env, const CMVector<DSPartitionCheckResult>& checks) {
    CMVector<const DSBlockBuildData*> blocks = {&env.top, &env.sub};
    CMVector<const DSNetBuildData*> nets = {&env.top_nets, &env.sub_nets};
    CMVector<const DSBlockNames*> names = {&env.top_names, &env.sub_names};
    return ds_verify_design(env.tree, env.design, env.stack, env.density,
                            env.net_union, blocks, nets, names,
                            check_ptrs(checks));
}

// ── 1. 分区级校验：计数 + id 素材集 ─────────────────────────────────

TEST(DSVerifyTest, PartitionCheckCountsAndIdSets) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    DSPartitionCheckResult r0 = ds_verify_partition(
        0, 0, 0, pp.p0.geometry_, pp.p0.instances_, pp.p0.inst_connections_,
        pp.p0.net_connections_);
    EXPECT_EQ(r0.partition_id_, 0u);
    EXPECT_EQ(r0.primary_instance_count_, 2u);          // 1/2 primary
    EXPECT_EQ(r0.instance_count_, 3u);                  // 1/2/3（3 = 副本）
    EXPECT_EQ(r0.geometry_entry_count_, 3u);            // 2 wire + 1 via
    EXPECT_EQ(r0.crossing_net_count_, 1u);              // net 0
    EXPECT_EQ(r0.connection_count_, 4u);                // iconn 2 + nconn 2
    // 网覆盖素材：geometry {0,1,2} ∪ nconn {0} ∪ iconn {0,1} ∪ crossing {0}
    ASSERT_EQ(r0.net_ids_.size(), 3u);
    EXPECT_EQ((CMVector<uint64_t>{r0.net_ids_[0], r0.net_ids_[1],
                                  r0.net_ids_[2]}),
              (CMVector<uint64_t>{0, 1, 2}));
    ASSERT_EQ(r0.instance_ids_.size(), 3u);
    ASSERT_EQ(r0.primary_instance_ids_.size(), 2u);
    EXPECT_EQ(r0.primary_instance_ids_[0], 1u);
    EXPECT_EQ(r0.primary_instance_ids_[1], 2u);

    DSPartitionCheckResult r1 = ds_verify_partition(
        1, 1, 0, pp.p1.geometry_, pp.p1.instances_, pp.p1.inst_connections_,
        pp.p1.net_connections_);
    EXPECT_EQ(r1.primary_instance_count_, 2u);          // 3/5 primary
    EXPECT_EQ(r1.instance_count_, 2u);
    EXPECT_EQ(r1.geometry_entry_count_, 2u);
    EXPECT_EQ(r1.crossing_net_count_, 0u);
    EXPECT_EQ(r1.connection_count_, 3u);                // iconn 2 + nconn 1
    ASSERT_EQ(r1.net_ids_.size(), 2u);
    EXPECT_EQ((CMVector<uint64_t>{r1.net_ids_[0], r1.net_ids_[1]}),
              (CMVector<uint64_t>{3, 4}));
}

TEST(DSVerifyTest, ObsOnlyNetZeroBucketNotCountedAsCoverage) {
    // net 0 桶仅含 OBS 条目时不计为网 0 覆盖（obs 位判别——root 首网与
    // OBS 同键共存，OBS 不是网几何）
    DSPartitionGeometry geometry;
    DSGeomEntry obs;
    obs.layer_id_ = 1;
    obs.rect_ = GEORect(10, 10, 20, 20);
    obs.set_obs();
    geometry.add_entry(0, std::move(obs));
    DSPartInstances instances;
    DSPartInstConnections iconns;
    DSPartNetConnections nconns;
    const DSPartitionCheckResult r = ds_verify_partition(
        0, 0, 0, geometry, instances, iconns, nconns);
    EXPECT_EQ(r.geometry_entry_count_, 1u);
    EXPECT_TRUE(r.net_ids_.empty());
}

// ── 2. 覆盖校验（损坏类 fatal）──────────────────────────────────────

TEST(DSVerifyTest, CoverageCompleteGridPasses) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_TRUE(report.coverage_gap_.empty());
}

TEST(DSVerifyTest, CoverageSinglePartitionPasses) {
    // 单分区 '1x1'：core = 全覆盖域（QA design 场景形态）
    VerifyEnv env;
    DSSubPartition whole;
    whole.partition_id_ = 0;
    whole.xp_ = 0;
    whole.yp_ = 0;
    whole.core_rect_ = GEORect(0, 0, 2000, 2000);
    env.parts.clear();
    env.parts.push_back(whole);
    env.design.partitions_ = env.parts;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_TRUE(report.coverage_gap_.empty());
}

TEST(DSVerifyTest, CoverageMissingPartitionFails) {
    VerifyEnv env;
    env.parts.pop_back();  // 缺 p1 → 网格不完整
    env.design.partitions_ = env.parts;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_FALSE(report.coverage_gap_.empty());
}

TEST(DSVerifyTest, CoverageInconsistentRowBoundaryFails) {
    // 行边界不一致：p1 core y_high 与 p0 不同（网格性质破坏）
    VerifyEnv env;
    env.parts[1].core_rect_ = GEORect(1000, 0, 2000, 1500);
    env.design.partitions_ = env.parts;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_FALSE(report.coverage_gap_.empty());
}

TEST(DSVerifyTest, CoverageDomainMisalignmentFails) {
    // 分区 core 并集 ≠ 全局格网覆盖域（bin 1000 × 3 列 → x_high 3000）
    VerifyEnv env;
    env.density.configure(0, 0, 1000, 1000, 3, 2);
    env.density.counts_.assign(6, 0);
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_FALSE(report.coverage_gap_.empty());
}

TEST(DSVerifyTest, CoverageEmptyDesignPasses) {
    // 无 DEF 建库：空分区表 + 未配置格网 → 放行（dev-rules §7）
    VerifyEnv env;
    env.parts.clear();
    env.design.partitions_ = env.parts;
    DSDensityGrid unconfigured;
    PartitionPair pp = make_products();
    CMVector<const DSBlockBuildData*> blocks = {&env.top, &env.sub};
    CMVector<const DSNetBuildData*> nets = {&env.top_nets, &env.sub_nets};
    CMVector<const DSBlockNames*> names = {&env.top_names, &env.sub_names};
    const DSDesignCheckReport report = ds_verify_design(
        env.tree, env.design, env.stack, unconfigured, env.net_union, blocks,
        nets, names, check_ptrs(check_products(pp)));
    EXPECT_TRUE(report.coverage_gap_.empty());
}

// ── 3. 并查集自洽（损坏类 fatal）────────────────────────────────────

TEST(DSVerifyTest, UnionConsistentPasses) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_TRUE(report.union_inconsistency_.empty());
}

TEST(DSVerifyTest, UnionSelfMapBrokenFails) {
    VerifyEnv env;
    env.net_union.root_of_[11] = 4;  // root 4 不在表 → 两层不变式破坏
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_FALSE(report.union_inconsistency_.empty());
}

TEST(DSVerifyTest, UnionMembersMismatchFails) {
    VerifyEnv env;
    // 反向索引漏登记成员 11：members 列表数 ≠ root_of_ 规模（双向破坏）
    env.net_union.members_of_[10] = CMVector<uint64_t>{10};
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_FALSE(report.union_inconsistency_.empty());
}

// ── 4. namemap 双向闭环（损坏类 fatal）──────────────────────────────

TEST(DSVerifyTest, NamemapClosurePasses) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_TRUE(report.namemap_inconsistency_.empty());
}

TEST(DSVerifyTest, NamemapOneSideBrokenFails) {
    // 单向断裂：伴生 instance hasher 的 id→name 槽位清空（白盒模拟落盘
    // 损坏）——get_name(1) = "" ≠ "la"
    VerifyEnv env;
    env.top_names.instance_names_->name_table_[1] = DSNameSlot{};
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_FALSE(report.namemap_inconsistency_.empty());
}

// ── 5. id 连续性（观测类 warn，不 fatal）────────────────────────────

TEST(DSVerifyTest, IdContinuityHolesAndDuplicatesCounted) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    // 实例：id 5 从两分区消失（空洞）；id 2 在 p1 增一 primary 副本
    //（多 primary 重复）。via：sub 的过孔产物消失（空洞 1）。
    pp.p1.instances_.items_.erase(5);
    DSInstance i2;
    i2.set_cell_id(0);
    i2.set_primary();
    pp.p1.instances_.items_[2] = std::move(i2);
    env.sub_nets.via_instances_.clear();  // sub 过孔消失（via 空洞 1）

    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    // 损坏类保持为空（观测类不 fatal——分级裁定红线）
    EXPECT_TRUE(report.union_inconsistency_.empty());
    EXPECT_TRUE(report.coverage_gap_.empty());
    EXPECT_TRUE(report.namemap_inconsistency_.empty());
    // 实例域：expected 4 / actual {1,2,3} = 3 → holes 1；
    // primary 直方图 {1:1, 2:2, 3:1} → duplicates 1
    EXPECT_EQ(report.instance_ids_.expected_, 4u);
    EXPECT_EQ(report.instance_ids_.actual_, 3u);
    EXPECT_EQ(report.instance_ids_.holes_, 1u);
    EXPECT_EQ(report.instance_ids_.duplicates_, 1u);
    // 网域：coverage 不受实例扰动影响 → holes 0
    EXPECT_EQ(report.net_ids_.expected_, 5u);
    EXPECT_EQ(report.net_ids_.actual_, 5u);
    EXPECT_EQ(report.net_ids_.holes_, 0u);
    // via 域：expected 3 / actual {0,1} = 2 → holes 1
    EXPECT_EQ(report.via_ids_.expected_, 3u);
    EXPECT_EQ(report.via_ids_.actual_, 2u);
    EXPECT_EQ(report.via_ids_.holes_, 1u);
    EXPECT_EQ(report.via_ids_.duplicates_, 0u);
}

// ── 6. 密度守恒（观测类 warn，primary 口径）─────────────────────────

TEST(DSVerifyTest, DensityConservationPrimaryPasses) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    // Σ primary = 2 + 2 = 4 = Σ 首份定义 (实例数 − UNPLACED) = 3 + 1
    EXPECT_TRUE(report.density_variance_.empty());
    EXPECT_EQ(report.total_primary_, 4u);
}

TEST(DSVerifyTest, DualInstantiationConservationPasses) {
    // review 2026-09-13 Blocker 2 锁定：同一 def 被实例化两次时守恒右端
    // 必须按树节点（实例化位置）求和——原按 def 求和少算 (k-1)×placed，
    // 合法库恒误报。树加 sub 第二实例化节点（inst_start 6 → u1@sub2
    // global 7），primary 素材相应补 7 → 右端 3+1+1 = 5 = 左端
    VerifyEnv env;
    DSHierNode sub2;
    sub2.id_ = 2;
    sub2.parent_id_ = 0;
    sub2.block_cell_name_ = "sub";
    sub2.instance_name_ = "csub2";
    sub2.self_global_id_ = 4;  // 假设 top 增第二个 csub 实例（id 4）
    sub2.instance_start_ = 6;
    sub2.instance_count_ = 2;
    sub2.net_start_ = 5;
    sub2.net_count_ = 2;
    sub2.via_start_ = 3;
    sub2.via_count_ = 1;
    env.tree.nodes_.push_back(sub2);
    env.tree.nodes_[0].get_ref_children_ids().push_back(2);

    PartitionPair pp = make_products();
    CMVector<DSPartitionCheckResult> checks = check_products(pp);
    checks[0].primary_instance_count_ += 1;  // u1@sub2 的 primary 落 p0
    checks[0].primary_instance_ids_.push_back(7);
    std::sort(checks[0].primary_instance_ids_.begin(),
              checks[0].primary_instance_ids_.end());
    checks[0].instance_count_ += 1;
    checks[0].instance_ids_.push_back(7);
    std::sort(checks[0].instance_ids_.begin(), checks[0].instance_ids_.end());

    const DSDesignCheckReport report = verify_env_design(env, checks);
    // 右端 = top 3 + sub×2 位置 ×1 = 5；左端 = 原 4 + sub2 的 1 = 5
    EXPECT_TRUE(report.density_variance_.empty())
        << report.density_variance_;
    EXPECT_EQ(report.total_primary_, 5u);
}

TEST(DSVerifyTest, EmptySegmentPartitionTableCoveragePasses) {
    // review 2026-09-13 Blocker 1 锁定：S8 空段机制（prefix_cuts 负载
    // 集中时多切线同值 → 零宽段跳过、xp_ 保留原网格坐标）产出形态合法
    // ——3 格域两分区（xp 0 与 xp 2，[0,1000)+[1000,3000) 并集全域无缝）
    // 覆盖校验放行；原满网格校验误判 (2,0) outside grid
    DSDensityGrid density;
    density.configure(0, 0, 1000, 1000, 3, 2);
    CMVector<DSSubPartition> parts;
    DSSubPartition p0;
    p0.partition_id_ = 0;
    p0.xp_ = 0;
    p0.yp_ = 0;
    p0.core_rect_ = GEORect(0, 0, 1000, 2000);
    DSSubPartition p2;
    p2.partition_id_ = 1;
    p2.xp_ = 2;  // 原网格坐标保留（xp1 零宽段被跳过）
    p2.yp_ = 0;
    p2.core_rect_ = GEORect(1000, 0, 3000, 2000);
    parts.push_back(p0);
    parts.push_back(p2);
    EXPECT_TRUE(ds_check_partition_coverage(parts, density).empty());
    // 对照：真实缺口（漏掉一段有宽度的分区）仍 fatal
    p2.core_rect_ = GEORect(2000, 0, 3000, 2000);  // [1000,2000) 缺口
    parts[1] = p2;
    EXPECT_FALSE(ds_check_partition_coverage(parts, density).empty());
}

TEST(DSVerifyTest, DensityVarianceWarned) {
    VerifyEnv env;
    env.sub.stats_.unplaced_count = 1;  // 树展开可放置数降为 3 ≠ primary 4
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_FALSE(report.density_variance_.empty());
    EXPECT_TRUE(report.union_inconsistency_.empty());
    EXPECT_TRUE(report.coverage_gap_.empty());
    EXPECT_TRUE(report.namemap_inconsistency_.empty());
}

// ── 7. 全局统计 + 报告序列化往返 ────────────────────────────────────

TEST(DSVerifyTest, GlobalStatsAggregated) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    EXPECT_EQ(report.partition_count_, 2u);
    EXPECT_EQ(report.total_primary_, 4u);
    EXPECT_EQ(report.total_instances_, 5u);        // 含副本（p0 的 3）
    EXPECT_EQ(report.total_nets_, 5u);             // 覆盖 distinct 网 id
    EXPECT_EQ(report.total_connections_, 7u);      // iconn 4 + nconn 3
    EXPECT_EQ(report.total_geometry_entries_, 5u);
    EXPECT_EQ(report.total_crossing_nets_, 1u);    // net 0 跨分区
    EXPECT_EQ(report.expected_instances_, 4u);
    EXPECT_EQ(report.expected_nets_, 5u);
    EXPECT_EQ(report.expected_vias_, 3u);
    EXPECT_EQ(report.density_instance_total_, 10u);
    EXPECT_EQ(report.density_metal_total_, 5u);
    EXPECT_EQ(report.density_via_total_, 7u);
}

TEST(DSVerifyTest, ReportSerializeRoundTrip) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));

    CMString blob;
    FLY_ENCODE(report, blob);
    DSDesignCheckReport back;
    FLY_DECODE(blob, DSDesignCheckReport, back);

    EXPECT_EQ(back.partition_count_, report.partition_count_);
    EXPECT_EQ(back.total_primary_, 4u);
    EXPECT_EQ(back.instance_ids_.expected_, 4u);
    EXPECT_EQ(back.instance_ids_.actual_, 4u);
    EXPECT_EQ(back.net_ids_.holes_, 0u);
    EXPECT_EQ(back.via_ids_.actual_, 3u);
    EXPECT_TRUE(back.density_variance_.empty());
    EXPECT_TRUE(back.union_inconsistency_.empty());
    EXPECT_EQ(back.density_instance_total_, 10u);
}

// ── 8. fatal 路径（fork 断言 rc=80；单测进程未绑定 fatal 分发）──────

TEST(DSVerifyTest, BrokenReportFatalsWithCode80) {
    DSDesignCheckReport bad;
    bad.union_inconsistency_ = "root_of_ two-layer invariant broken";
    fly::test::expect_fatal_exit_code([&] { ds_verify_report_or_fatal(bad); },
                                      80);
}

TEST(DSVerifyTest, CoverageReportFatalsWithCode80) {
    DSDesignCheckReport bad;
    bad.coverage_gap_ = "missing partition (1,0)";
    fly::test::expect_fatal_exit_code([&] { ds_verify_report_or_fatal(bad); },
                                      80);
}

TEST(DSVerifyTest, NamemapReportFatalsWithCode80) {
    DSDesignCheckReport bad;
    bad.namemap_inconsistency_ = "cell hasher closure broken";
    fly::test::expect_fatal_exit_code([&] { ds_verify_report_or_fatal(bad); },
                                      80);
}

TEST(DSVerifyTest, CleanReportDoesNotFatal) {
    VerifyEnv env;
    PartitionPair pp = make_products();
    const DSDesignCheckReport report = verify_env_design(env,
                                                         check_products(pp));
    ds_verify_report_or_fatal(report);  // 干净报告正常返回（进程存活）
}

}  // namespace
