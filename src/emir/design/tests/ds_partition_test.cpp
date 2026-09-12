// S8 全局密度图合并 + 分区决策单测（方案 design-db-plan.md §4 +
// 2026-09-12/13 裁定）：
//   1. 密度合并：对齐平移直拷贝、非对齐面积比例分摊（D10 A，手算）、
//      最大余数法守恒（小计数/tie 规则）、同一 def 多次实例化、旋转
//      （W 朝向宽高换轴）、三通道独立分列；
//   2. 分区决策：'{x}x{y}' 直切（2x1/2x2）、w_eff 取最高有效层
//      （M1 有效 M2 无效 → 2w = 140）、6:2:2 合成数值、partition_count
//      行列分布（负载等效宽高比 + 空段跳过）、target_density 推导 N、
//      空负载兜底单分区、非法 alpha 回退（DSGN::0013 提醒不 raise）；
//   3. DSSubPartition / DSDesign.partitions_ 序列化往返。
// 全部期望值按实现口径手工推导（格网 bin=100 手算），锁定行为。
#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_partition.h>
#include <emir/design/cpp/ds_types.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <utility>

namespace {

using namespace fly;

constexpr int32_t kIntMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kIntMax = std::numeric_limits<int32_t>::max();

// GEORectT 无 operator==（纯几何层），逐字段断言
void expect_rect(const char* what, const GEORect& r, int32_t xl, int32_t yl,
                 int32_t xh, int32_t yh) {
    EXPECT_EQ(r.get_x_low(), xl) << what;
    EXPECT_EQ(r.get_y_low(), yl) << what;
    EXPECT_EQ(r.get_x_high(), xh) << what;
    EXPECT_EQ(r.get_y_high(), yh) << what;
}

// —— 合并辅助：手工构造树 + per-DEF 产物（不依赖 DEF 文件）──────────

DSBlockBuildData make_block_data(const char* name) {
    DSBlockBuildData b;
    b.init_placeholder(name, DSDesign::kInvalidId);
    return b;
}

// 生产接口借指针（CMVector<T> → CMVector<const T*>，同 ds_hier_test 先例）
CMVector<const DSBlockBuildData*> block_ptrs(
    const CMVector<DSBlockBuildData>& v) {
    CMVector<const DSBlockBuildData*> out;
    for (const auto& b : v) out.push_back(&b);
    return out;
}

CMVector<const DSNetBuildData*> net_ptrs(const CMVector<DSNetBuildData>& v) {
    CMVector<const DSNetBuildData*> out;
    for (const auto& n : v) out.push_back(&n);
    return out;
}

// root(top) → sub：root inst [0,2)（local 1 = 子实例 self 1）；sub inst
// [2,3)（local 0 占位）
struct TwoNodeEnv {
    DSHierTree tree;
    CMVector<DSBlockBuildData> blocks;
    CMVector<DSNetBuildData> nets;

    explicit TwoNodeEnv(GEOTransform sub_placement) {
        DSHierNode root;
        root.id_ = 0;
        root.parent_id_ = 0;
        root.block_cell_name_ = "top";
        root.instance_start_ = 0;
        root.instance_count_ = 2;
        tree.nodes_.push_back(root);
        DSHierNode sub;
        sub.id_ = 1;
        sub.parent_id_ = 0;
        sub.block_cell_name_ = "sub";
        sub.self_global_id_ = 1;  // ⑧：父块 inst 区间内的 local 1
        sub.instance_start_ = 2;
        sub.instance_count_ = 1;
        tree.nodes_.push_back(sub);

        blocks.push_back(make_block_data("top"));
        DSInstance child;
        child.set_transform(sub_placement);
        blocks[0].add_instance(std::move(child), "c1");
        blocks.push_back(make_block_data("sub"));
        nets.resize(2);
    }
};

// 格网快捷配置：bin 恒 100；v != 0 时全格统一置值
DSDensityGrid make_grid(int32_t ox, int32_t oy, uint32_t cols, uint32_t rows,
                        int64_t v) {
    DSDensityGrid g;
    g.configure(ox, oy, 100, 100, cols, rows);
    if (v != 0) {
        g.counts_.assign(static_cast<size_t>(cols) * rows, v);
    }
    return g;
}

// —— 决策辅助：标准 stack（M1 ROUTING w70 / VIA1 CUT / M2 ROUTING w80）─

DSStack make_stack() {
    DSStack stack;
    DSLayer m1;
    m1.name_ = "M1";
    m1.type_ = static_cast<uint8_t>(DSLayerType::ROUTING);
    m1.default_width_ = 70;
    stack.add_layer(std::move(m1));
    DSLayer via1;
    via1.name_ = "VIA1";
    via1.type_ = static_cast<uint8_t>(DSLayerType::CUT);
    stack.add_layer(std::move(via1));
    DSLayer m2;
    m2.name_ = "M2";
    m2.type_ = static_cast<uint8_t>(DSLayerType::ROUTING);
    m2.default_width_ = 80;
    stack.add_layer(std::move(m2));
    return stack;
}

// 标准图：counts r0=[12,0,6,0] r1=[8,0,4,0]；metal M1（层 0）全格 1
// （w_eff = 70）；6:2:2 合成 col_load = [124,4,64,4]、total = 196
DSDensityGrid make_decision_grid() {
    DSDensityGrid g;
    g.configure(0, 0, 100, 100, 4, 2);
    g.counts_ = {12, 0, 6, 0, 8, 0, 4, 0};
    g.metal_layer_counts_[0].assign(8, 1);
    return g;
}

// ── 1. 合并：对齐平移直拷贝 + 三通道独立 ────────────────────────────

TEST(DSMergeGlobalDensityTest, AlignedTranslationCopiesCells) {
    TwoNodeEnv env(GEOTransform(GEOPoint(100, 0), GEOOrientation::N));
    env.blocks[0].density_ = make_grid(0, 0, 2, 1, 0);
    env.blocks[0].density_.counts_[0] = 1;
    env.blocks[1].density_ = make_grid(0, 0, 1, 1, 2);
    // 三通道独立：金属层 5、通孔层 6 各自分列叠加
    env.nets[0].density_ = make_grid(0, 0, 2, 1, 0);
    env.nets[0].density_.metal_layer_counts_[5] = {3, 0};
    env.nets[1].density_ = make_grid(0, 0, 1, 1, 0);
    env.nets[1].density_.metal_layer_counts_[5] = {7};
    env.nets[1].density_.via_layer_counts_[6] = {9};

    const DSDensityGrid global =
        ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                net_ptrs(env.nets));

    // 全局格网 = 根 def 局部格网（origin/cols/rows/bin 全同）
    EXPECT_EQ(global.get_cols(), 2u);
    EXPECT_EQ(global.get_rows(), 1u);
    EXPECT_EQ(global.get_origin_x(), 0);
    EXPECT_EQ(global.get_bin_width(), 100);
    // 实例通道：root (0,0)=1 + sub 平移 (100,0) → (1,0)=2
    EXPECT_EQ(global.cell_count(0, 0), 1);
    EXPECT_EQ(global.cell_count(1, 0), 2);
    // 金属通道独立
    ASSERT_NE(global.metal_layer_counts_.find(5),
              global.metal_layer_counts_.end());
    EXPECT_EQ(global.layer_total(5, false), 10);
    // 通孔通道独立（root 无 via，sub 平移后 (1,0)=9）
    ASSERT_NE(global.via_layer_counts_.find(6), global.via_layer_counts_.end());
    EXPECT_EQ(global.layer_total(6, true), 9);
}

// ── 2. 合并：非对齐分摊（D10 A 面积比例，手算）─────────────────────

TEST(DSMergeGlobalDensityTest, PartialOverlapSplitsByArea) {
    // sub 平移 (50,0)：局部格 [0,100)² → 全局 [50,150)×[0,100)
    // 与格 0 交叠 50×100=5000、格 1 交叠 5000 → base 各 = 1000×5000/1e4 = 500
    TwoNodeEnv env(GEOTransform(GEOPoint(50, 0), GEOOrientation::N));
    env.blocks[0].density_ = make_grid(0, 0, 2, 1, 0);
    env.blocks[0].density_.counts_[0] = 1;  // root 仅 (0,0)=1
    env.blocks[1].density_ = make_grid(0, 0, 1, 1, 1000);

    const DSDensityGrid global =
        ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                net_ptrs(env.nets));
    EXPECT_EQ(global.cell_count(0, 0), 1 + 500);
    EXPECT_EQ(global.cell_count(1, 0), 500);
    // 守恒：总量 = root 1 + sub 1000
    EXPECT_EQ(global.total_count(), 1001);
}

TEST(DSMergeGlobalDensityTest, LargestRemainderKeepsTotal) {
    // v=1 不可整除：平移 (25,0) → 交叠 7500/2500，ideal = 0.75/0.25、
    // base 0/0，余数（小数部分）大者格 0 +1
    {
        TwoNodeEnv env(GEOTransform(GEOPoint(25, 0), GEOOrientation::N));
        env.blocks[0].density_ = make_grid(0, 0, 2, 1, 0);
        env.blocks[0].density_.counts_[0] = 1;  // root 仅 (0,0)=1
        env.blocks[1].density_ = make_grid(0, 0, 1, 1, 1);
        const DSDensityGrid global =
            ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                    net_ptrs(env.nets));
        EXPECT_EQ(global.cell_count(0, 0), 2);
        EXPECT_EQ(global.cell_count(1, 0), 0);
        EXPECT_EQ(global.total_count(), 2);
    }
    // tie（平移 (50,0) 交叠相等）：行主序小者优先 → 格 0 +1
    {
        TwoNodeEnv env(GEOTransform(GEOPoint(50, 0), GEOOrientation::N));
        env.blocks[0].density_ = make_grid(0, 0, 2, 1, 0);
        env.blocks[0].density_.counts_[0] = 1;  // root 仅 (0,0)=1
        env.blocks[1].density_ = make_grid(0, 0, 1, 1, 1);
        const DSDensityGrid global =
            ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                    net_ptrs(env.nets));
        EXPECT_EQ(global.cell_count(0, 0), 2);
        EXPECT_EQ(global.cell_count(1, 0), 0);
        EXPECT_EQ(global.total_count(), 2);
    }
}

// ── 3. 合并：同一 def 多次实例化 ────────────────────────────────────

TEST(DSMergeGlobalDensityTest, SameDefInstantiatedTwice) {
    // top（1×3 格网，(0,0)=1）实例化 sub（1×1，值 10）两次：pos (100,0)/
    // (200,0) → 全局 [1, 10, 10]
    DSHierTree tree;
    DSHierNode root;
    root.id_ = 0;
    root.parent_id_ = 0;
    root.block_cell_name_ = "top";
    root.instance_start_ = 0;
    root.instance_count_ = 3;
    tree.nodes_.push_back(root);
    for (uint32_t k = 0; k < 2; ++k) {
        DSHierNode sub;
        sub.id_ = k + 1;
        sub.parent_id_ = 0;
        sub.block_cell_name_ = "sub";
        sub.self_global_id_ = k + 1;
        sub.instance_start_ = 3;
        sub.instance_count_ = 1;
        tree.nodes_.push_back(sub);
    }

    CMVector<DSBlockBuildData> blocks;
    DSBlockBuildData top = make_block_data("top");
    top.density_ = make_grid(0, 0, 3, 1, 0);
    top.density_.counts_[0] = 1;
    for (int k = 0; k < 2; ++k) {
        DSInstance inst;
        inst.set_transform(
            GEOTransform(GEOPoint(100 * (k + 1), 0), GEOOrientation::N));
        top.add_instance(std::move(inst), "c" + std::to_string(k));
    }
    DSBlockBuildData sub = make_block_data("sub");
    sub.density_ = make_grid(0, 0, 1, 1, 10);
    blocks.push_back(std::move(top));
    blocks.push_back(std::move(sub));
    CMVector<DSNetBuildData> nets(2);

    const DSDensityGrid global = ds_merge_global_density(tree, block_ptrs(blocks), net_ptrs(nets));
    EXPECT_EQ(global.cell_count(0, 0), 1);
    EXPECT_EQ(global.cell_count(1, 0), 10);
    EXPECT_EQ(global.cell_count(2, 0), 10);
    EXPECT_EQ(global.total_count(), 21);
}

// ── 4. 合并：旋转（W 朝向，宽高换轴）───────────────────────────────

TEST(DSMergeGlobalDensityTest, RotatedPlacementScattersCorrectly) {
    // W 变换：局部格 [0,100)² → R_W box [−100,0)×[0,100)；pos (100,100)
    // → 全局 [0,100)×[100,200) = 格 (0,1)
    TwoNodeEnv env(GEOTransform(GEOPoint(100, 100), GEOOrientation::W));
    env.blocks[0].density_ = make_grid(0, 0, 2, 2, 0);
    env.blocks[1].density_ = make_grid(0, 0, 1, 1, 3);

    const DSDensityGrid global =
        ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                net_ptrs(env.nets));
    EXPECT_EQ(global.cell_count(0, 0), 0);
    EXPECT_EQ(global.cell_count(0, 1), 3);
    EXPECT_EQ(global.cell_count(1, 0), 0);
    EXPECT_EQ(global.total_count(), 3);
}

// ── 5. 合并：空树兜底 ───────────────────────────────────────────────

TEST(DSMergeGlobalDensityTest, EmptyTreeReturnsUnconfiguredGrid) {
    CMVector<DSBlockBuildData> blocks;
    CMVector<DSNetBuildData> nets;
    DSHierTree empty;
    const DSDensityGrid global = ds_merge_global_density(empty, {}, {});
    EXPECT_EQ(global.get_cols(), 0u);
}

TEST(DSMergeGlobalDensityTest, PartialOutOfDomainScatterTruncates) {
    // 子格矩形经平移部分越出全局格网覆盖域（review 2026-09-13 断言域
    // 修复的锁定用例）：sub 1×1 值 1000 放置 (150,0)——格矩形 [150,250)
    // 与全局格 1 [100,200) 交叠 5000/10000，另一半越界。截断口径：
    // 格 1 = floor(1000×5000/10000)=500 + 余额 min(500, 交叠格数 1)=1
    // → 501；越界 499 丢弃（格网域外不计，不再 abort）
    TwoNodeEnv env(GEOTransform(GEOPoint(150, 0), GEOOrientation::N));
    env.blocks[0].density_ = make_grid(0, 0, 2, 1, 0);
    env.blocks[1].density_ = make_grid(0, 0, 1, 1, 1000);

    const DSDensityGrid global =
        ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                net_ptrs(env.nets));
    ASSERT_EQ(global.get_cols(), 2u);
    EXPECT_EQ(global.counts_[0], 0);
    EXPECT_EQ(global.counts_[1], 501);
}

TEST(DSMergeGlobalDensityTest, SizeMismatchReturnsUnconfigured) {
    // nets/blocks 尺寸不匹配：防御分支 ERR + 空图（对齐错误在树构建期
    // 已 fatal，此处防御空转）
    TwoNodeEnv env(GEOTransform(GEOPoint(0, 0), GEOOrientation::N));
    env.blocks[0].density_ = make_grid(0, 0, 2, 1, 3);
    env.blocks[1].density_ = make_grid(0, 0, 1, 1, 0);
    CMVector<const DSNetBuildData*> nets_only_one = {&env.nets[0]};
    const DSDensityGrid global =
        ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                nets_only_one);
    EXPECT_EQ(global.get_cols(), 0u);
}

TEST(DSMergeGlobalDensityTest, RootWithoutDieAreaReturnsUnconfigured) {
    // 根 def 无 DIEAREA（格网未配置）：空图（空结果放行）
    TwoNodeEnv env(GEOTransform(GEOPoint(0, 0), GEOOrientation::N));
    env.blocks[0].density_ = make_grid(0, 0, 0, 0, 0);  // 未配置
    env.blocks[1].density_ = make_grid(0, 0, 1, 1, 5);
    const DSDensityGrid global =
        ds_merge_global_density(env.tree, block_ptrs(env.blocks),
                                net_ptrs(env.nets));
    EXPECT_EQ(global.get_cols(), 0u);
}

// ── 6. 决策：'{x}x{y}' 直切 + w_eff 取最高有效层 ────────────────────

TEST(DSDecidePartitionsTest, SinglePartitionByCountOne) {
    // partition_count = 1：single 分支——core = 全包围盒（格网全域），
    // extend 全向 int32 极值（最外围分区四向皆外缘，裁定 2）。消费方
    // 禁止对 extend 调 width()/height()（int32 减法溢出 UB——见
    // DSSubPartition 字段注释），断言用坐标分量
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "", 1, 0);
    ASSERT_EQ(parts.size(), 1u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 400, 200);
    EXPECT_EQ(parts[0].extend_rect_.get_x_low(), kIntMin);
    EXPECT_EQ(parts[0].extend_rect_.get_y_low(), kIntMin);
    EXPECT_EQ(parts[0].extend_rect_.get_x_high(), kIntMax);
    EXPECT_EQ(parts[0].extend_rect_.get_y_high(), kIntMax);
}

TEST(DSDecidePartitionsTest, DirectCut2x1WithEffectiveLayerWidth) {
    // 金属只在 M1（层表更高的 M2 无值）→ w_eff = M1 default_width 70，
    // 2w = 140；合成 col_load = [124,4,64,4]，'2x1' 等分 98 → col0 前缀
    // 124 ≥ 98 → 切线 = 格 1 右边界
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();
    const DSDensityWeights weights;  // 默认 6/2/2

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, weights, "2x1", 0, 0);
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].partition_id_, 0u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 100, 200);
    // 最外围方向 int32 极值；内侧 x 方向 +2w
    expect_rect("p0 extend", parts[0].extend_rect_, kIntMin, kIntMin, 240,
                kIntMax);
    EXPECT_EQ(parts[1].partition_id_, 1u);
    expect_rect("p1 core", parts[1].core_rect_, 100, 0, 400, 200);
    // 内侧 −2w；右边缘极值
    expect_rect("p1 extend", parts[1].extend_rect_, -40, kIntMin, kIntMax,
                kIntMax);
}

TEST(DSDecidePartitionsTest, DirectCut2x2RowMajorIds) {
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "2x2", 0, 0);
    // 行负载 [116, 80]，ny=2 等分 98 → row0 前缀 116 ≥ 98 → 行切线 1
    ASSERT_EQ(parts.size(), 4u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 100, 100);
    expect_rect("p1 core", parts[1].core_rect_, 100, 0, 400, 100);
    expect_rect("p2 core", parts[2].core_rect_, 0, 100, 100, 200);
    expect_rect("p3 core", parts[3].core_rect_, 100, 100, 400, 200);
    // id 行主序
    EXPECT_EQ(parts[0].partition_id_, 0u);
    EXPECT_EQ(parts[1].partition_id_, 1u);
    EXPECT_EQ(parts[2].partition_id_, 2u);
    EXPECT_EQ(parts[3].partition_id_, 3u);
    // p3 = (xp1, yp1) 双最外围（nx=ny=2 的末位）：x_high/y_high 极值、
    // 内侧 x_low/y_low 扩 2w
    expect_rect("p3 extend", parts[3].extend_rect_, -40, -40, kIntMax,
                kIntMax);
    // (0,0) 分区 x_low/y_low 最外围极值
    expect_rect("p0 extend", parts[0].extend_rect_, kIntMin, kIntMin, 240, 240);
}

// ── 7. 决策：6:2:2 合成数值（权重敏感性）────────────────────────────

TEST(DSDecidePartitionsTest, CompositeWeightsDriveCutLine) {
    // 仅实例权重（metal/via 0）：col_load = [20,0,10,0] 总 30，'2x2' 列
    // 等分 15 → col0 前缀 20 ≥ 15 → 列切线 1；行负载 [18,12] 等分 15 →
    // 行切线 1
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();
    DSDensityWeights weights;
    weights.instance_ = 1.0;
    weights.metal_ = 0.0;
    weights.via_ = 0.0;

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, weights, "2x2", 0, 0);
    ASSERT_EQ(parts.size(), 4u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 100, 100);
    expect_rect("p3 core", parts[3].core_rect_, 100, 100, 400, 200);
    // w_eff 由金属通道计数判定（权重不影响 w_eff）→ 内侧扩 2w = 140
    expect_rect("p0 extend", parts[0].extend_rect_, kIntMin, kIntMin, 240, 240);
}

TEST(DSDecidePartitionsTest, ViaChannelEntersCompositeLoad) {
    // via 权重敏感性：仅通孔通道（实例/金属清零）via 层 1 值
    // {5,5,0,0,5,5,0,0} → 合成 col_load = [20,20,0,0]（2×每列 10），
    // '2x1' 等分 20 → col0 前缀 20 ≥ 20 → 切线 1
    DSDensityGrid global = make_decision_grid();
    global.counts_.assign(8, 0);         // 实例清零
    global.metal_layer_counts_.clear();  // 金属清零 → w_eff = 0
    global.via_layer_counts_[1] = {5, 5, 0, 0, 5, 5, 0, 0};  // VIA1 层 1
    const DSStack stack = make_stack();
    const DSDensityWeights weights;  // 6/2/2（仅 via 项非零贡献）

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, weights, "2x1", 0, 0);
    ASSERT_EQ(parts.size(), 2u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 100, 200);
    expect_rect("p1 core", parts[1].core_rect_, 100, 0, 400, 200);
    // 金属通道无值 → w_eff = 0 → 内侧扩 0
    expect_rect("p0 extend", parts[0].extend_rect_, kIntMin, kIntMin, 100,
                kIntMax);
    expect_rect("p1 extend", parts[1].extend_rect_, 100, kIntMin, kIntMax,
                kIntMax);
}

// ── 8. 决策：partition_count 行列分布（负载等效宽高比）──────────────

TEST(DSDecidePartitionsTest, PartitionCountAdaptsToLoadShape) {
    // 均匀 6×2（合成每格 6）：Wq = 2σ_col = 2×1.7078 ≈ 3.416、Hq = 1 →
    // nx = round(√(4×3.416)) = round(3.696) = 4、ny = 1；列切线（total
    // 36 等分 9）→ cuts [0,2,3,5,6]（前缀 6/12/18/24/30 跨 9/18/27）
    DSDensityGrid global;
    global.configure(0, 0, 100, 100, 6, 2);
    global.counts_.assign(12, 1);  // 均匀（仅实例通道）
    const DSStack stack = make_stack();

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "", 4, 0);
    ASSERT_EQ(parts.size(), 4u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 200, 200);
    expect_rect("p1 core", parts[1].core_rect_, 200, 0, 300, 200);
    expect_rect("p2 core", parts[2].core_rect_, 300, 0, 500, 200);
    expect_rect("p3 core", parts[3].core_rect_, 500, 0, 600, 200);
}

TEST(DSDecidePartitionsTest, EmptySegmentSkippedWhenCutsRepeat) {
    // 负载全集中 col0（counts [10,0,0,0]，1×4）、N=3：Wq = max(0,1) = 1、
    // Hq = 1 → nx = round(√3) = 2、ny = ceil(3/2) = 2；列切线（等分 30：
    // c0 前缀 60 ≥ 30 → 切线 1，k 用尽后尾部切线钳到末界）→ 列切线
    // [0,1,4]（nx=2 仅 1 条内部切线）；行单格切不出 2 段 → [0,1,1]，
    // 第二行段空不产出 → 仅 2 分区
    DSDensityGrid global;
    global.configure(0, 0, 100, 100, 4, 1);
    global.counts_ = {10, 0, 0, 0};
    const DSStack stack = make_stack();

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "", 3, 0);
    ASSERT_EQ(parts.size(), 2u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 100, 100);
    expect_rect("p1 core", parts[1].core_rect_, 100, 0, 400, 100);
}

TEST(DSDecidePartitionsTest, TargetDensityDerivesCount) {
    // 总合成 196 / td=150 → N = ceil(1.307) = 2 → 负载等效比定 2×1
    //（列负载强不均：Wq = max(2σ,1) ≈ 1.97 vs 行负载 [116,80] 的 2σ ≈
    // 0.983 → Hq = 1（下限）→ nx = round(√(2×1.97)) ≈ round(1.985) = 2）
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "", 0, 150);
    ASSERT_EQ(parts.size(), 2u);
    expect_rect("p0 core", parts[0].core_rect_, 0, 0, 100, 200);
    expect_rect("p1 core", parts[1].core_rect_, 100, 0, 400, 200);
}

TEST(DSDecidePartitionsTest, HugeTargetDensityYieldsSinglePartition) {
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();

    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "", 0,
                             1000000);
    ASSERT_EQ(parts.size(), 1u);
    expect_rect("core", parts[0].core_rect_, 0, 0, 400, 200);
    // 单分区全向最外围：extend = int32 极值
    expect_rect("extend", parts[0].extend_rect_, kIntMin, kIntMin, kIntMax,
                kIntMax);
}

// ── 9. 决策：空负载兜底 + 非法 alpha 回退 ───────────────────────────

TEST(DSDecidePartitionsTest, EmptyLoadFallsBackToSinglePartition) {
    DSDensityGrid global;
    global.configure(0, 0, 100, 100, 4, 2);  // 全零计数
    const DSStack stack = make_stack();

    // 显式 '2x2' 也被兜底覆盖（无负载信息可切）
    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "2x2", 0, 0);
    ASSERT_EQ(parts.size(), 1u);
    expect_rect("core", parts[0].core_rect_, 0, 0, 400, 200);
    expect_rect("extend", parts[0].extend_rect_, kIntMin, kIntMin, kIntMax,
                kIntMax);
}

TEST(DSDecidePartitionsTest, IllegalTargetPartitionsFallsBack) {
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();

    // 非法串逐个回退 partition_count=2 路径（WARN DSGN::0013 不 raise），
    // 结果与 TargetDensityDerivesCount 同形（N=2 → 2×1）
    for (const char* bad :
         {"abc", "0x2", "2", "2x", "2xx1", "2x1x3", "x2", "2x0"}) {
        const CMVector<DSSubPartition> parts = ds_decide_partitions(
            global, stack, DSDensityWeights{}, bad, 2, 0);
        ASSERT_EQ(parts.size(), 2u) << "input: " << bad;
        expect_rect(bad, parts[0].core_rect_, 0, 0, 100, 200);
        expect_rect(bad, parts[1].core_rect_, 100, 0, 400, 200);
    }
}

TEST(DSDecidePartitionsTest, IllegalTargetDensityFallsBackToDefault) {
    DSDensityGrid global = make_decision_grid();
    const DSStack stack = make_stack();

    // 负值非法 → 回退默认 150000；总合成 196 < 150000 → N=1 单分区
    const CMVector<DSSubPartition> parts =
        ds_decide_partitions(global, stack, DSDensityWeights{}, "", 0, -5);
    ASSERT_EQ(parts.size(), 1u);
    expect_rect("core", parts[0].core_rect_, 0, 0, 400, 200);
}

// ── 10. 序列化往返 ──────────────────────────────────────────────────

TEST(DSSubPartitionTest, SerializeRoundTrip) {
    DSSubPartition p;
    p.partition_id_ = 7;
    p.core_rect_ = GEORect(10, -20, 300, 400);
    p.extend_rect_ = GEORect(kIntMin, -160, kIntMax, 540);

    CMString blob;
    FLY_ENCODE(p, blob);
    DSSubPartition back;
    FLY_DECODE(blob, DSSubPartition, back);

    EXPECT_EQ(back.partition_id_, 7u);
    expect_rect("core", back.core_rect_, 10, -20, 300, 400);
    expect_rect("extend", back.extend_rect_, kIntMin, -160, kIntMax, 540);
}

TEST(DSDesignPartitionsTest, PartitionsSurviveDesignRoundTrip) {
    DSDesign design;
    DSCell cell;
    cell.set_name("c");
    design.add_cell(std::move(cell));

    DSSubPartition p0;
    p0.partition_id_ = 0;
    p0.core_rect_ = GEORect(0, 0, 100, 200);
    p0.extend_rect_ = GEORect(kIntMin, kIntMin, 240, kIntMax);
    DSSubPartition p1;
    p1.partition_id_ = 1;
    p1.core_rect_ = GEORect(100, 0, 400, 200);
    p1.extend_rect_ = GEORect(-40, kIntMin, kIntMax, kIntMax);
    CMVector<DSSubPartition> parts;
    parts.push_back(p0);
    parts.push_back(p1);
    design.set_partitions(std::move(parts));

    CMString blob;
    FLY_ENCODE(design, blob);
    DSDesign back;
    FLY_DECODE(blob, DSDesign, back);

    ASSERT_EQ(back.partition_count(), 2u);
    EXPECT_EQ(back.partition_at(0).partition_id_, 0u);
    expect_rect("p0 core", back.partition_at(0).core_rect_, 0, 0, 100, 200);
    expect_rect("p1 extend", back.partition_at(1).extend_rect_, -40, kIntMin,
                kIntMax, kIntMax);
}

}  // namespace
