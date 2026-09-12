// S5a COMPONENTS 责任链 ∥ 网名扫描单测：
//   1. 链骨架：顺序执行 + 错误标记即停（方案 design-db-phase2-plan §2.1）；
//   2. CellResolveNode：namemap 命中 / fake cell 生成（⑲/⑳：名
//      block::cell、1×1 bbox、fake_cell 位、id = max_cell_id + hash
//      高位扰动 + 递增）/ 同 DEF 复用 / 跨 DEF id 互异；
//   3. InstanceBuildNode：local id 从 1 起（⑧）、place_from_def 换算
//      （R6）、status/weight 填充；
//   4. DensityNode：实例 footprint 与固定采样格子的交叠计数（图形计数
//      口径 ⑥，半开区间；UNPLACED 不计，D14）；
//   5. StatsNode：per-cell 计数 / fake / UNPLACED 统计；
//   6. DSBlockBuildData（⑬ per-DEF 独立对象）序列化往返；
//   7. 汇总并入 ds_merge_block_build：fake id 保持任务内分配值、冲突
//      顺延 + instance 引用重映射；
//   8. 适配层 ds_parse_def_components：真 DEF 全链（COMPONENTS 回调 + 网名
//      扫描同遍读取）。
// 数据：data/block_synth.def（复用，UNITS=1000/恒基准 DBU=1000 ×1 换算）、
//       data/components_synth.def（UNPLACED/WEIGHT/重名网兜底）。
#include <emir/design/cpp/ds_def_adapter.h>
#include <emir/design/cpp/ds_instance_pipeline.h>
#include <emir/design/cpp/ds_lef_adapter.h>
#include <emir/design/cpp/ds_merge.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <set>
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

// 标准测试 design：INV_X1（手工构造 bbox 1400×1400，不涉文件换算）
DSDesign make_design() {
    DSDesign design;
    DSCell inv;
    inv.set_name("INV_X1");
    inv.set_lef_cell();
    inv.set_bbox(GEORect(0, 0, 1400, 1400));
    design.add_cell(std::move(inv));
    return design;
}

// 构造挂好环境的最小 ctx（local 0 占位由 init_placeholder 完成）
DSInstanceContext make_ctx(DSDesign& design, DSBlockBuildData& block_data,
                           const char* block_name) {
    DSInstanceContext ctx;
    ctx.design = &design;
    ctx.block_data = &block_data;
    ctx.block_name = block_name;
    block_data.init_placeholder(block_name,
                                DSDesign::kInvalidId);  // ⑧ local 0 占位
    return ctx;
}

// ── 1. 链骨架：顺序执行 + 错误即停 ──────────────────────────────────

class OrderRecorderNode : public DSInstanceHandler {
public:
    explicit OrderRecorderNode(std::string tag, bool fail = false)
        : tag_(std::move(tag)), fail_(fail) {}
    const char* name() const override { return tag_.c_str(); }
    void handle(DSInstanceContext& ctx) override {
        order->push_back(tag_);
        if (fail_) ctx.error = true;
    }
    CMVector<std::string>* order = nullptr;

private:
    std::string tag_;
    bool fail_;
};

TEST(DSInstancePipelineTest, RunsInOrderAndStopsOnError) {
    DSInstancePipeline pipeline;
    CMVector<std::string> order;
    auto a = std::make_unique<OrderRecorderNode>("a", false);
    auto b = std::make_unique<OrderRecorderNode>("b", true);
    auto c = std::make_unique<OrderRecorderNode>("c", false);
    a->order = &order;
    b->order = &order;
    c->order = &order;
    pipeline.add(std::move(a));
    pipeline.add(std::move(b));
    pipeline.add(std::move(c));

    DSInstanceContext ctx;
    pipeline.run(ctx);

    // 顺序执行；b 置错误后 c 不再执行
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "a");
    EXPECT_EQ(order[1], "b");
    EXPECT_TRUE(ctx.error);
}

// ── 2. CellResolveNode ──────────────────────────────────────────────

TEST(DSCellResolveNodeTest, ResolvesMasterFromNamemap) {
    DSDesign design = make_design();
    DSBlockBuildData block_data;
    DSInstanceContext ctx = make_ctx(design, block_data, "blk");

    ctx.master_name = "INV_X1";
    DSCellResolveNode node;
    node.handle(ctx);

    EXPECT_FALSE(ctx.error);
    EXPECT_EQ(ctx.cell_id, 0u);  // INV_X1 = 首个 add_cell
    // cell 几何填充（InstanceBuild 的换算输入）
    EXPECT_EQ(ctx.cell_bbox.get_x_high(), 1400);
    EXPECT_EQ(ctx.cell_origin_x, 0);
    EXPECT_TRUE(block_data.fake_cells_.empty());
}

TEST(DSCellResolveNodeTest, CreatesFakeCellForUndefinedMaster) {
    DSDesign design = make_design();
    DSBlockBuildData block_data;
    DSInstanceContext ctx = make_ctx(design, block_data, "blk");

    ctx.master_name = "GHOST";
    DSCellResolveNode node;
    node.handle(ctx);

    EXPECT_FALSE(ctx.error);
    // fake cell（⑲/⑳）：block::cell 命名、1×1 最小单位矩形、fake 位、
    // 无 pin、id = max_cell_id(1) + hash 扰动 + 递增
    ASSERT_EQ(block_data.fake_cells_.size(), 1u);
    const DSCell& fake = block_data.fake_cells_[0];
    EXPECT_EQ(fake.get_name(), "blk::GHOST");
    EXPECT_EQ(fake.get_bbox().get_x_low(), 0);
    EXPECT_EQ(fake.get_bbox().get_x_high(), 1);
    EXPECT_EQ(fake.get_bbox().get_y_high(), 1);
    EXPECT_TRUE(fake.is_fake_cell());
    EXPECT_EQ(fake.pin_count(), 0u);
    const uint32_t fake_id = block_data.fake_name_to_id_.at("blk::GHOST");
    EXPECT_EQ(ctx.cell_id, fake_id);
    EXPECT_GE(fake_id, 2u);  // > max_cell_id
    EXPECT_LE(fake_id, 2u + 65536u + 1u);  // hash 空间 + 递增上界
    EXPECT_EQ(block_data.stats_.fake_cell_count, 1u);
    // fake 的几何进 ctx（1×1、origin 0）
    EXPECT_EQ(ctx.cell_bbox.get_x_high(), 1);
    EXPECT_EQ(ctx.cell_origin_x, 0);
}

TEST(DSCellResolveNodeTest, ReusesFakeWithinSameDef) {
    DSDesign design = make_design();
    DSBlockBuildData block_data;
    DSInstanceContext ctx = make_ctx(design, block_data, "blk");
    DSCellResolveNode node;

    ctx.master_name = "GHOST";
    node.handle(ctx);
    const uint32_t first_id = ctx.cell_id;

    // 同一 master 第二次出现：复用已生成 fake，不再新增
    DSInstanceContext ctx2 = make_ctx(design, block_data, "blk");
    ctx2.master_name = "GHOST";
    node.handle(ctx2);

    EXPECT_EQ(ctx2.cell_id, first_id);
    EXPECT_EQ(block_data.fake_cells_.size(), 1u);
    EXPECT_EQ(block_data.stats_.fake_cell_count, 1u);
}

TEST(DSCellResolveNodeTest, FakeIdsDifferAcrossDefTasks) {
    // 并行 per-DEF 任务各自生成：不同 block 名 hash 扰动 → id 互异（⑳，
    // 冲突率不严格；确定性 hash + 固定测试名 = 结果确定）
    DSDesign design = make_design();
    DSBlockBuildData b1;
    DSBlockBuildData b2;
    DSInstanceContext c1 = make_ctx(design, b1, "block_one");
    DSInstanceContext c2 = make_ctx(design, b2, "block_two");
    c1.master_name = "GHOST";
    c2.master_name = "GHOST";

    DSCellResolveNode node;
    node.handle(c1);
    node.handle(c2);

    EXPECT_NE(c1.cell_id, c2.cell_id);
    EXPECT_GE(c1.cell_id, 2u);
    EXPECT_GE(c2.cell_id, 2u);
}

// ── 3. InstanceBuildNode ────────────────────────────────────────────

TEST(DSInstanceBuildNodeTest, AssignsLocalIdsFromOneAndBuildsTransform) {
    DSDesign design = make_design();
    DSBlockBuildData block_data;
    DSInstanceContext ctx = make_ctx(design, block_data, "blk");
    DSCellResolveNode resolve;
    DSInstanceBuildNode build;

    // 第一实例：t=(200,400) N → INV box (0,0,1400,1400) ll=(0,0)
    // → pos = (200,400)
    ctx.instance_name = "i1";
    ctx.master_name = "INV_X1";
    ctx.placement = GEOPoint(200, 400);
    ctx.orient = GEOOrientation::N;
    ctx.placement_status = static_cast<uint8_t>(DSPlacementStatus::PLACED);
    ctx.weight = 3.0;
    resolve.handle(ctx);
    build.handle(ctx);

    EXPECT_EQ(ctx.instance_id, 1u);  // ⑧ local id 从 1 起（local 0 = 占位）
    const DSInstance& inst = block_data.instances_.at(1);
    // R7 ㊱：DSInstance 无 name——实例名经双向 instance hasher 查回
    EXPECT_EQ(block_data.instance_names_->get_name(1), "i1");
    EXPECT_EQ(inst.get_cell_id(), 0u);
    EXPECT_EQ(inst.get_transform().get_offset().get_x(), 200);
    EXPECT_EQ(inst.get_transform().get_offset().get_y(), 400);
    EXPECT_EQ(inst.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::PLACED));
    EXPECT_DOUBLE_EQ(inst.get_weight(), 3.0);
    EXPECT_EQ(block_data.instance_names_->get_id("i1"), 1u);
    // local 0 = block 自身占位（⑧；占位不进 instance hasher——非真实
    // 实例，R7 ㊱）
    EXPECT_EQ(block_data.instance_names_->get_id("blk"),
              DSInstanceNameHasher::kInvalidId);
    EXPECT_TRUE(block_data.instances_.contains(0));

    // 第二实例：local id 递增；FS 换算（fake cell 1×1：R_FS box ll=(0,−1)）
    DSInstanceContext ctx2 = make_ctx(design, block_data, "blk");
    ctx2.instance_name = "i2";
    ctx2.master_name = "GHOST";  // fake
    ctx2.placement = GEOPoint(600, 800);
    ctx2.orient = GEOOrientation::FS;
    resolve.handle(ctx2);
    build.handle(ctx2);
    EXPECT_EQ(ctx2.instance_id, 2u);
    const DSInstance& inst2 = block_data.instances_.at(2);
    EXPECT_EQ(inst2.get_cell_id(), ctx2.cell_id);
    EXPECT_EQ(inst2.get_transform().get_offset().get_x(), 600);
    EXPECT_EQ(inst2.get_transform().get_offset().get_y(), 801);
}

// ── 4. DensityNode：DSDensityGrid 计数 ──────────────────────────────

TEST(DSDensityGridTest, AccumulatesOverlappingBinsHalfOpen) {
    DSDensityGrid grid;
    grid.configure(0, 0, 100, 100, 10, 10);

    // footprint 跨格 (2..5)x(1..3)（半开区间）：x∈[200,600) y∈[100,400)
    grid.accumulate_footprint(GEORect(200, 100, 600, 400));
    // 与格共享边界不算交叠（[600,700) 列不计）
    EXPECT_EQ(grid.total_count(), 4u * 3u);
    EXPECT_EQ(grid.cell_count(2, 1), 1);
    EXPECT_EQ(grid.cell_count(5, 3), 1);
    EXPECT_EQ(grid.cell_count(6, 1), 0);
    EXPECT_EQ(grid.cell_count(2, 4), 0);

    // 第二个实例叠加累计
    grid.accumulate_footprint(GEORect(200, 100, 600, 400));
    EXPECT_EQ(grid.cell_count(2, 1), 2);
    EXPECT_EQ(grid.total_count(), 2u * 4u * 3u);
}

TEST(DSDensityGridTest, ClampsNegativeFootprint) {
    DSDensityGrid grid;
    grid.configure(0, 0, 100, 100, 4, 4);
    // 越出网格原点的 footprint：负方向截断到 [0, cols)
    grid.accumulate_footprint(GEORect(-250, -50, 150, 250));
    // x 交叠格 [0,2)（−250 截到 0；150 → 格 1），y 交叠格 [0,3)
    EXPECT_EQ(grid.total_count(), 2u * 3u);
    EXPECT_EQ(grid.cell_count(0, 0), 1);
    EXPECT_EQ(grid.cell_count(1, 2), 1);
    EXPECT_EQ(grid.cell_count(2, 0), 0);
}

// S8 前置修正（2026-09-12 裁定 5）：block instance 自身 bbox 不计密度——
// block 的密度贡献 = S8 子树叠加（子块实例/网密度按放置平移撒入），此处
// 再计 block footprint 会双计。判定 = cell 的 block_cell 位。
TEST(DSDensityNodeTest, SkipsBlockInstanceFootprint) {
    DSDesign design = make_design();
    DSCell blk_cell;
    blk_cell.set_name("sub_blk");
    blk_cell.set_block_cell();
    blk_cell.set_bbox(GEORect(0, 0, 5000, 5000));
    design.add_cell(std::move(blk_cell));
    DSBlockBuildData block_data;
    block_data.init_placeholder("blk", DSDesign::kInvalidId);
    block_data.density_.configure(0, 0, 1000, 1000, 4, 4);
    DSInstancePipeline pipeline;
    pipeline.add(std::make_unique<DSCellResolveNode>());
    pipeline.add(std::make_unique<DSInstanceBuildNode>());
    pipeline.add(std::make_unique<DSDensityNode>());
    pipeline.add(std::make_unique<DSStatsNode>());

    // block instance（sub_blk）照常入表/统计，但不入密度通道
    DSInstanceContext ctx;
    ctx.design = &design;
    ctx.block_data = &block_data;
    ctx.block_name = "blk";
    ctx.instance_name = "b1";
    ctx.master_name = "sub_blk";
    ctx.placement = GEOPoint(0, 0);
    ctx.orient = GEOOrientation::N;
    ctx.placement_status = static_cast<uint8_t>(DSPlacementStatus::PLACED);
    pipeline.run(ctx);

    EXPECT_FALSE(ctx.error);
    EXPECT_EQ(ctx.instance_id, 1u);
    EXPECT_EQ(block_data.stats_.instance_count, 1u);
    EXPECT_EQ(block_data.density_.total_count(), 0);

    // 同一 pipeline 上 leaf instance 仍正常计入（对照）
    DSInstanceContext ctx2 = ctx;
    ctx2.instance_name = "i1";
    ctx2.master_name = "INV_X1";
    ctx2.placement = GEOPoint(2000, 2000);
    pipeline.run(ctx2);
    // INV footprint (2000,2000)-(3400,3400) → 跨格 2×2 = 4
    EXPECT_EQ(block_data.density_.total_count(), 4);
}

TEST(DSDensityNodeTest, CountsPlacedFootprintSkipsUnplaced) {
    DSDesign design = make_design();
    DSBlockBuildData block_data;
    block_data.init_placeholder("blk", DSDesign::kInvalidId);
    block_data.density_.configure(0, 0, 1000, 1000, 4, 4);
    DSInstancePipeline pipeline;
    pipeline.add(std::make_unique<DSCellResolveNode>());
    pipeline.add(std::make_unique<DSInstanceBuildNode>());
    pipeline.add(std::make_unique<DSDensityNode>());
    pipeline.add(std::make_unique<DSStatsNode>());

    // i1：INV @N t=(2000,2000) → footprint (2000,2000)-(3400,3400)
    // → 跨格 col 2..3 row 2..3 → 4 格各 +1
    DSInstanceContext ctx;
    ctx.design = &design;
    ctx.block_data = &block_data;
    ctx.block_name = "blk";
    ctx.instance_name = "i1";
    ctx.master_name = "INV_X1";
    ctx.placement = GEOPoint(2000, 2000);
    ctx.orient = GEOOrientation::N;
    ctx.placement_status = static_cast<uint8_t>(DSPlacementStatus::PLACED);
    pipeline.run(ctx);

    EXPECT_EQ(block_data.density_.total_count(), 4);
    // i2：UNPLACED 不计（D14）
    DSInstanceContext ctx2 = ctx;
    ctx2.instance_name = "i2";
    ctx2.placement = GEOPoint(0, 0);
    ctx2.placement_status = static_cast<uint8_t>(DSPlacementStatus::UNPLACED);
    pipeline.run(ctx2);
    EXPECT_EQ(block_data.density_.total_count(), 4);
    EXPECT_EQ(block_data.stats_.unplaced_count, 1u);
}

// ── 5. StatsNode ────────────────────────────────────────────────────

TEST(DSStatsNodeTest, CountsPerCellFakeAndUnplaced) {
    DSDesign design = make_design();
    DSBlockBuildData block_data;
    block_data.init_placeholder("blk", DSDesign::kInvalidId);
    DSInstancePipeline pipeline;
    pipeline.add(std::make_unique<DSCellResolveNode>());
    pipeline.add(std::make_unique<DSInstanceBuildNode>());
    pipeline.add(std::make_unique<DSStatsNode>());

    const auto run_one = [&](const char* name, const char* master,
                             DSPlacementStatus status) {
        DSInstanceContext ctx;
        ctx.design = &design;
        ctx.block_data = &block_data;
        ctx.block_name = "blk";
        ctx.instance_name = name;
        ctx.master_name = master;
        ctx.placement_status = static_cast<uint8_t>(status);
        pipeline.run(ctx);
    };

    run_one("i1", "INV_X1", DSPlacementStatus::PLACED);
    run_one("i2", "INV_X1", DSPlacementStatus::FIXED);
    run_one("i3", "GHOST", DSPlacementStatus::UNPLACED);  // fake + UNPLACED

    EXPECT_EQ(block_data.stats_.instance_count, 3u);
    EXPECT_EQ(block_data.stats_.unplaced_count, 1u);
    EXPECT_EQ(block_data.stats_.fake_cell_count, 1u);
    EXPECT_EQ(block_data.stats_.per_cell_counts_.at(0), 2u);   // INV_X1
    EXPECT_EQ(block_data.stats_.per_cell_counts_.size(), 2u);  // + fake id
}

// ── 6. DSBlockBuildData 序列化往返（⑬ 独立对象，落盘能力锚定）───────

TEST(DSBlockBuildDataTest, SerializeRoundTrip) {
    DSDesign design = make_design();
    DSBlockBuildData block_data;
    block_data.init_placeholder("blk", DSDesign::kInvalidId);
    block_data.density_.configure(0, 0, 1000, 1000, 4, 4);
    DSInstancePipeline pipeline;
    pipeline.add(std::make_unique<DSCellResolveNode>());
    pipeline.add(std::make_unique<DSInstanceBuildNode>());
    pipeline.add(std::make_unique<DSDensityNode>());
    pipeline.add(std::make_unique<DSStatsNode>());

    DSInstanceContext ctx;
    ctx.design = &design;
    ctx.block_data = &block_data;
    ctx.block_name = "blk";
    ctx.instance_name = "i1";
    ctx.master_name = "INV_X1";
    ctx.placement = GEOPoint(2000, 2000);
    ctx.orient = GEOOrientation::W;
    ctx.placement_status = static_cast<uint8_t>(DSPlacementStatus::PLACED);
    ctx.weight = 1.5;
    pipeline.run(ctx);
    block_data.register_net("n1");
    block_data.register_net("n2");

    CMString blob;
    FLY_ENCODE(block_data, blob);
    ASSERT_EQ(block_data.density_.total_count(), 4);  // 往返前计数就位
    DSBlockBuildData back;
    FLY_DECODE(blob, DSBlockBuildData, back);

    EXPECT_EQ(back.get_block_name(), "blk");
    ASSERT_EQ(back.instance_total(), 2u);  // 占位 0 + i1
    // R7 ㊱/㊵②：占位不进 instance hasher；hasher 不在 DSBlock_<i> 序列
    // 化面（名字经 DSBlockNames 伴生对象）
    EXPECT_EQ(back.instance_names_, nullptr);  // 读回未 attach = 空
    const DSInstance& inst = back.instances_.at(1);
    // W 变换：box (0,0,1400,1400) → R_W ll=(−1400,0) → pos=(3400,2000)
    EXPECT_EQ(inst.get_transform().get_offset().get_x(), 3400);
    EXPECT_EQ(inst.get_transform().get_offset().get_y(), 2000);
    EXPECT_EQ(inst.get_transform().get_orient(), GEOOrientation::W);
    EXPECT_DOUBLE_EQ(inst.get_weight(), 1.5);
    // ㊵②：网名空间同样不随 DSBlock_<i> 落盘（运行时 hasher 经伴生对象
    // 注入）——读回后 net 查询为空，名字由 DSBlockNames_<i> 承载
    EXPECT_EQ(back.net_names_, nullptr);  // 读回未 attach = 空
    EXPECT_EQ(back.net_count(), 0u);
    EXPECT_EQ(back.density_.total_count(), 4);
    EXPECT_EQ(back.stats_.instance_count, 1u);
    EXPECT_EQ(back.next_instance_id_, 2u);
    EXPECT_EQ(back.next_net_id_, 3u);
}

// ── 7. 汇总并入：fake id 保持 + 冲突顺延 + 引用重映射 ───────────────

TEST(DSMergeBlockBuildTest, MergesFakeCellsKeepingAssignedIds) {
    DSDesign design = make_design();  // cells_ = [INV_X1]，max_cell_id = 0
    DSBlockBuildData b1;
    DSInstanceContext c1 = make_ctx(design, b1, "blk");
    c1.master_name = "GHOST";
    DSCellResolveNode resolve;
    DSInstanceBuildNode build;
    resolve.handle(c1);
    build.handle(c1);
    const uint32_t assigned = c1.cell_id;

    EXPECT_EQ(ds_merge_block_build(design, b1), 1);
    // id 保持任务内分配值（namemap 命中 + cells_ 稀疏落位 + fake 索引）
    EXPECT_EQ(design.cell_names_.get_id("blk::GHOST"), assigned);
    EXPECT_EQ(design.cells_[assigned].get_name(), "blk::GHOST");
    EXPECT_TRUE(design.cells_[assigned].is_fake_cell());
    ASSERT_EQ(design.fake_cell_ids_.size(), 1u);
    EXPECT_EQ(design.fake_cell_ids_[0], assigned);
    // instance 引用保持（id 未冲突，无需重映射）
    EXPECT_EQ(b1.instances_.at(1).get_cell_id(), assigned);
}

TEST(DSMergeBlockBuildTest, ResolvesFakeIdConflictByShifting) {
    // 两个 block_data 生成同 id 的 fake：算法 = base + hash(def 名) + seq，
    // 同 block 名 + 同 seq 天然同 id（⑳：冲突率不严格，汇总顺延兜底）
    DSDesign design = make_design();
    DSBlockBuildData b1;
    DSBlockBuildData b2;
    DSInstanceContext c1 = make_ctx(design, b1, "blk");
    DSInstanceContext c2 = make_ctx(design, b2, "blk");
    c1.master_name = "GHOST_A";
    c2.master_name = "GHOST_B";
    DSCellResolveNode resolve;
    DSInstanceBuildNode build;
    resolve.handle(c1);
    build.handle(c1);
    resolve.handle(c2);
    build.handle(c2);
    // 同 block 名 + 同 seq（各自首个 fake）→ 任务内分配值相同（冲突场景）
    ASSERT_EQ(c1.cell_id, c2.cell_id);
    const uint32_t conflict_id = c1.cell_id;

    EXPECT_EQ(ds_merge_block_build(design, b1), 1);
    EXPECT_EQ(ds_merge_block_build(design, b2), 1);
    // b1 占住 conflict_id；b2 顺延到下一个空位
    const uint32_t shifted = b2.fake_name_to_id_.at("blk::GHOST_B");
    EXPECT_EQ(shifted, conflict_id + 1);
    EXPECT_EQ(design.cell_names_.get_id("blk::GHOST_A"), conflict_id);
    EXPECT_EQ(design.cell_names_.get_id("blk::GHOST_B"), shifted);
    EXPECT_EQ(b2.instances_.at(1).get_cell_id(), shifted);  // 引用重映射
    EXPECT_EQ(design.fake_cell_ids_.size(), 2u);
    // 同名 fake 重复并入被跳过（防御）
    EXPECT_EQ(ds_merge_block_build(design, b2), 0);
    EXPECT_EQ(design.fake_cell_ids_.size(), 2u);
}

TEST(DSDesignAddCellAtTest, SparsePlacementKeepsIdSemantics) {
    DSDesign design;
    DSCell a;
    a.set_name("a");
    design.add_cell(std::move(a));
    DSCell fake;
    fake.set_name("x::y");
    fake.set_fake_cell();
    design.add_cell_at(5, std::move(fake));
    // id = 下标语义保持；空洞为占位（空名）
    EXPECT_EQ(design.cells_.size(), 6u);
    EXPECT_EQ(design.cell_names_.get_id("x::y"), 5u);
    EXPECT_EQ(design.cell_names_.get_name(5), "x::y");
    EXPECT_EQ(design.get_cell(5).get_name(), "x::y");
    EXPECT_TRUE(design.cells_[2].get_name().empty());
    EXPECT_EQ(design.cell_names_.get_name(2), "");
    // 低位落位（无 resize）
    DSCell b;
    b.set_name("b");
    design.add_cell_at(1, std::move(b));
    EXPECT_EQ(design.cell_names_.get_id("b"), 1u);
}

// ── 8. 适配层全链（COMPONENTS 回调 ∥ 网名扫描，同一遍读取）──────────

// block_synth.def：UNITS=1000、恒基准 DBU=1000（×1）；INV_X1 已定义
//（bbox (0,0,1400,1400)），DFF_X1 故意不定义 → fake。
TEST(DsDefComponentsTest, ParsesComponentsAndNetNamesOnePass) {
    DSStack stack;
    CMVector<DSViaCell> tech_vias;
    ds_parse_tech_lef(test_data("tech_synth.lef").string(), stack, tech_vias);

    DSDesign design = make_design();
    DSBlockBuildData block_data;
    DSDefComponentsStats stats;
    ds_parse_def_components(test_data("block_synth.def").string(), stack, design,
                     block_data, stats, 1000);

    EXPECT_EQ(stats.component_count, 2);
    EXPECT_EQ(stats.net_count, 2);
    EXPECT_EQ(stats.skipped_net_count, 0);

    // block 名来自 DESIGN 语句；local 0 占位（⑧；占位不进 hasher）
    EXPECT_EQ(block_data.get_block_name(), "block_a");
    EXPECT_TRUE(block_data.instances_.contains(0));

    // inst1 INV_X1 + PLACED (100,200) N：t=(100,200)（×1）→ pos=(100,200)
    // R7 ㊱：实例名经双向 instance hasher 查回
    const DSInstance& i1 = block_data.instances_.at(1);
    EXPECT_EQ(block_data.instance_names_->get_name(1), "inst1");
    EXPECT_EQ(block_data.instance_names_->get_id("inst1"), 1u);
    EXPECT_EQ(i1.get_cell_id(), 0u);
    EXPECT_EQ(i1.get_transform().get_offset().get_x(), 100);
    EXPECT_EQ(i1.get_transform().get_offset().get_y(), 200);
    EXPECT_EQ(i1.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::PLACED));
    EXPECT_DOUBLE_EQ(i1.get_weight(), 0.0);

    // inst2 DFF_X1 + PLACED (300,400) FS：未定义 → fake；t=(300,400)，
    // fake 1×1 box 经 R_FS ll=(0,−1) → pos=(300,401)
    const DSInstance& i2 = block_data.instances_.at(2);
    EXPECT_EQ(block_data.instance_names_->get_name(2), "inst2");
    ASSERT_EQ(block_data.fake_cells_.size(), 1u);
    EXPECT_EQ(block_data.fake_cells_[0].get_name(), "block_a::DFF_X1");
    EXPECT_EQ(i2.get_cell_id(), block_data.fake_name_to_id_.at(
                                    "block_a::DFF_X1"));
    EXPECT_EQ(i2.get_transform().get_offset().get_x(), 300);
    EXPECT_EQ(i2.get_transform().get_offset().get_y(), 401);
    EXPECT_EQ(i2.get_transform().get_orient(), GEOOrientation::FS);

    // 网名扫描（③ NetNameOnly）：local net id 从 1 起
    ASSERT_TRUE(block_data.net_names_ != nullptr);
    EXPECT_EQ(block_data.net_names_->get_id("n1"), 1u);
    EXPECT_EQ(block_data.net_names_->get_id("n2"), 2u);

    // 统计
    EXPECT_EQ(block_data.stats_.instance_count, 2u);
    EXPECT_EQ(block_data.stats_.fake_cell_count, 1u);
    // 密度格：DIEAREA ×1 = (−1000,−500)-(1500,2000)，bin 1000 → 3×3；
    // inst1 footprint (100,200)-(1500,1600) → 跨格 2×3 = 6；inst2 1×1 → 1
    EXPECT_EQ(block_data.density_.get_cols(), 3u);
    EXPECT_EQ(block_data.density_.get_rows(), 3u);
    EXPECT_EQ(block_data.density_.total_count(), 7);
}

// components_synth.def：UNPLACED（无坐标）/ WEIGHT / FIXED / COVER / NETS 重名
// 保留首份 / SPECIALNETS 网名。
TEST(DsDefComponentsTest, HandlesUnplacedWeightDuplicateNets) {
    DSStack stack;
    CMVector<DSViaCell> tech_vias;
    ds_parse_tech_lef(test_data("tech_synth.lef").string(), stack, tech_vias);

    DSDesign design = make_design();
    DSBlockBuildData block_data;
    DSDefComponentsStats stats;
    ds_parse_def_components(test_data("components_synth.def").string(), stack, design,
                     block_data, stats, 1000);

    EXPECT_EQ(stats.component_count, 4);
    EXPECT_EQ(block_data.stats_.instance_count, 4u);
    EXPECT_EQ(block_data.stats_.unplaced_count, 1u);
    EXPECT_EQ(block_data.stats_.fake_cell_count, 1u);  // GHOST_CELL

    // u1：WEIGHT 3 + PLACED N；t=(100,100) → pos=(100,100)
    const DSInstance& u1 = block_data.instances_.at(1);
    EXPECT_DOUBLE_EQ(u1.get_weight(), 3.0);
    EXPECT_EQ(u1.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::PLACED));
    EXPECT_EQ(u1.get_transform().get_offset().get_x(), 100);
    EXPECT_EQ(u1.get_transform().get_offset().get_y(), 100);

    // u2：FIXED FS；t=(200,100)、INV box 1400×1400 → R_FS ll=(0,−1400)
    // → pos=(200,1500)
    const DSInstance& u2 = block_data.instances_.at(2);
    EXPECT_EQ(u2.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::FIXED));
    EXPECT_EQ(u2.get_transform().get_offset().get_x(), 200);
    EXPECT_EQ(u2.get_transform().get_offset().get_y(), 1500);

    // u3：UNPLACED（defi 置坐标 (−1,−1) orient −1）→ 钳制 N、照收入表
    const DSInstance& u3 = block_data.instances_.at(3);
    EXPECT_EQ(u3.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::UNPLACED));
    EXPECT_EQ(u3.get_transform().get_orient(), GEOOrientation::N);

    // u4：COVER S（GHOST fake 1×1）：t=(300,300) → R_S box ll=(−1,−1)
    // → pos=(301,301)
    const DSInstance& u4 = block_data.instances_.at(4);
    EXPECT_EQ(u4.get_placement_status(),
              static_cast<uint8_t>(DSPlacementStatus::COVER));
    EXPECT_EQ(u4.get_transform().get_offset().get_x(), 301);
    EXPECT_EQ(u4.get_transform().get_offset().get_y(), 301);

    // 网名：NETS n1 重名保留首份 + SPECIALNETS VDD；id 从 1 连续分配
    EXPECT_EQ(stats.net_count, 2);
    EXPECT_EQ(stats.skipped_net_count, 1);
    ASSERT_TRUE(block_data.net_names_ != nullptr);
    EXPECT_EQ(block_data.net_names_->get_id("n1"), 1u);
    EXPECT_EQ(block_data.net_names_->get_id("VDD"), 2u);

    // UNPLACED 不入密度；格网 DIEAREA ×1 = (0,0)-(1000,1000) bin 1000
    // → 1×1：u1/u2/u4 footprint 各 clamp 进唯一格 → 3
    EXPECT_EQ(block_data.density_.get_cols(), 1u);
    EXPECT_EQ(block_data.density_.get_rows(), 1u);
    EXPECT_EQ(block_data.density_.total_count(), 3);
}

TEST(DsDefComponentsTest, UnreadableFileRaises) {
    DSStack stack;
    DSDesign design;
    DSBlockBuildData block_data;
    DSDefComponentsStats stats;
    EXPECT_THROW(ds_parse_def_components("/nonexistent/no.def", stack, design,
                                  block_data, stats, 1000),
                 std::runtime_error);
}

}  // namespace
