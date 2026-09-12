// S6 层级树构建 + 起始编号分配单测（裁定 ⑧⑨⑮，方案 design-db-plan.md
// S6 节 + design-knowledge.md §5）：
//   1. 构建：从 S5a per-DEF 数据自根 DFS 展开——block 定义的每次引用 =
//      一个 block instance 节点（定义 DAG 共享、实例层面为树）；主 DEF
//      判定 = 唯一无父者（多根/零根 → fatal DSGN::0011 码 80 退出，D22
//      2026-09-12 裁定改 fatal message）+ 环检测；
//   2. 起始编号：深度优先序连续分配、区间连续不重叠；区间长度 = 该
//      block 定义的 instance/net/via instance 三类计数（instance 含
//      local 0 占位槽；net/via local id 从 1 起；via 计数 = S5b 产物
//      统计，与 S5a 计数同构入参）；
//   3. ⑧ local 0 映射：root local 0 → global 0；非 root local 0 → 该
//      block instance 自身的 global id（存于父块 instance 区间内）；
//   4. 四接口：区间反查（block_of_*）/ 范围查（*_range）/ parent 与
//      children / format_tree 以 name 打印缩进层级文本（含 via 计数为
//      0 的 block：零长区间不参与反查）；
//   5. global id 换算 API（⑨ local id + 起始编号，S9 flatten 输入口）；
//   6. DSHierTree 序列化 round-trip（⑬ 挂 DSDesign 容器持久化）。
// 构造：三层嵌套 block（top → mid ×2 → bottom，同一定义多次实例化的
// DAG → 树展开）纯合成 DSBlockBuildData/DSNetBuildData，不依赖 DEF 文件。
#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_types.h>

#include <common/testing/cpp/test_helpers.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using namespace fly;

// 合成 per-DEF 产物：init_placeholder + 实例（cell id 直填 = local 1..k）
// + 网名
DSBlockBuildData make_block(const char* name,
                            const std::vector<uint32_t>& instance_cell_ids,
                            size_t net_count) {
    DSBlockBuildData b;
    b.init_placeholder(name, DSDesign::kInvalidId);
    for (const uint32_t cell_id : instance_cell_ids) {
        DSInstance inst;
        inst.set_cell_id(cell_id);
        // R7 ㊱：DSInstance 不存 name——实例名经 add_instance 登记
        // instance hasher（per-DEF local 名空间）
        b.add_instance(std::move(inst),
                       "i" + std::to_string(b.next_instance_id_));
    }
    for (size_t i = 0; i < net_count; ++i) {
        b.register_net("n" + std::to_string(i));
    }
    return b;
}

// 合成 per-DEF 网内容产物（S6 仅消费 via instance 统计计数，S5b 产出）
DSNetBuildData make_net(uint64_t via_instance_count) {
    DSNetBuildData n;
    n.stats_.via_instance_count = via_instance_count;
    return n;
}

// 三层嵌套标准场景：
//   design cells：leaf(0, 普通)、mid(1, block_cell)、bottom(2, block_cell)
//   def0 top    ：local 1 = leaf、local 2 = mid、local 3 = leaf；2 网；
//                 2 via instance
//   def1 mid    ：local 1 = bottom、local 2 = bottom（同定义两次实例化）；
//                 1 网；1 via instance
//   def2 bottom ：local 1 = leaf；1 网；0 via instance（零计数块）
// 实例层面树（DFS 前序 = 节点 id）：
//   #0 top(self 0, inst [0,4), net [0,2), via [0,2))
//    └ #1 mid#1(self 2, inst [4,7), net [2,3), via [2,3))
//        ├ #2 bottom#1(self 5, inst [7,9), net [3,4), via [3,3))
//        └ #3 bottom#2(self 6, inst [9,11), net [4,5), via [3,3))
struct HierEnv {
    DSDesign design;
    // def 序 = def_paths 序；root（top）故意不排首位（主 DEF 判定按
    // 唯一无父者，非文件序）
    CMVector<DSBlockBuildData> blocks;
    CMVector<DSNetBuildData> nets;  // 与 blocks 同序对齐（S5b 产物）

    HierEnv() {
        // block cell 名 = DEF DESIGN 名（S4 语义）：mid/bottom
        DSCell leaf;
        leaf.set_name("leaf");
        design.add_cell(std::move(leaf));
        DSCell mid;
        mid.set_name("mid");
        mid.set_block_cell();
        design.add_cell(std::move(mid));
        DSCell bot;
        bot.set_name("bottom");
        bot.set_block_cell();
        design.add_cell(std::move(bot));

        blocks.push_back(make_block("top", {0, 1, 0}, 2));     // def0
        blocks.push_back(make_block("mid", {2, 2}, 1));        // def1
        blocks.push_back(make_block("bottom", {0}, 1));        // def2
        nets.push_back(make_net(2));
        nets.push_back(make_net(1));
        nets.push_back(make_net(0));
    }
};

// blocks/nets 借指针（与生产接口同形态）
struct HierPtrs {
    CMVector<const DSBlockBuildData*> blocks;
    CMVector<const DSNetBuildData*> nets;
    explicit HierPtrs(HierEnv& env) {
        for (auto& b : env.blocks) blocks.push_back(&b);
        for (auto& n : env.nets) nets.push_back(&n);
    }
};

// ── 1. 构建：DAG → 树展开 + DFS 编号 ────────────────────────────────

TEST(DSHierTreeTest, BuildsTreeFromDefsDfsNumbering) {
    HierEnv env;
    HierPtrs ptrs(env);
    const DSHierTree tree = ds_build_hier_tree(ptrs.blocks, ptrs.nets,
                                               env.design);

    ASSERT_EQ(tree.node_count(), 4u);
    EXPECT_EQ(tree.get_design_name(), "top");

    // 根 = 唯一无父者 top（非 def_paths 首位——主 DEF 判定按引用关系）
    const DSHierNode& root = tree.node(0);
    EXPECT_EQ(root.get_block_cell_name(), "top");
    EXPECT_EQ(root.get_instance_name(), "top");  // root 实例名 = block 名
    EXPECT_EQ(root.get_parent_id(), 0u);         // root 无父（自指哨兵）
    EXPECT_EQ(root.get_self_global_id(), 0u);    // ⑧ global id 0 = 根

    // 同一定义两次实例化 → 两个节点（实例层面为树）
    const DSHierNode& mid = tree.node(1);
    EXPECT_EQ(mid.get_block_cell_name(), "mid");
    EXPECT_EQ(mid.get_instance_name(), "i2");  // top 的 local 2 实例名
    EXPECT_EQ(mid.get_parent_id(), 0u);
    EXPECT_EQ(mid.get_self_global_id(), 2u);  // 父 inst [0,4) 内的 local 2

    const DSHierNode& b1 = tree.node(2);
    EXPECT_EQ(b1.get_block_cell_name(), "bottom");
    EXPECT_EQ(b1.get_instance_name(), "i1");
    EXPECT_EQ(b1.get_parent_id(), 1u);
    EXPECT_EQ(b1.get_self_global_id(), 5u);  // mid inst [4,7) 内 local 1

    const DSHierNode& b2 = tree.node(3);
    EXPECT_EQ(b2.get_self_global_id(), 6u);  // mid inst [4,7) 内 local 2

    // 直系 children
    ASSERT_EQ(root.get_children_ids().size(), 1u);  // 仅 mid 是 block instance
    EXPECT_EQ(root.get_children_ids()[0], 1u);
    ASSERT_EQ(mid.get_children_ids().size(), 2u);
    EXPECT_EQ(mid.get_children_ids()[0], 2u);
    EXPECT_EQ(mid.get_children_ids()[1], 3u);
}

// ── 2. 起始编号：DFS 序连续分配、区间连续不重叠（三类同验）──────────

TEST(DSHierTreeTest, AssignsContiguousNonOverlappingRangesInDfsOrder) {
    HierEnv env;
    HierPtrs ptrs(env);
    const DSHierTree tree = ds_build_hier_tree(ptrs.blocks, ptrs.nets,
                                               env.design);

    // instance 区间（含 local 0 占位槽）：top 4 + mid 3 + bottom×2 各 2
    const auto [i0s, i0c] = tree.instance_range(0);
    const auto [i1s, i1c] = tree.instance_range(1);
    const auto [i2s, i2c] = tree.instance_range(2);
    const auto [i3s, i3c] = tree.instance_range(3);
    EXPECT_EQ(i0s, 0u);
    EXPECT_EQ(i0c, 4u);
    EXPECT_EQ(i1s, 4u);
    EXPECT_EQ(i1c, 3u);
    EXPECT_EQ(i2s, 7u);
    EXPECT_EQ(i2c, 2u);
    EXPECT_EQ(i3s, 9u);
    EXPECT_EQ(i3c, 2u);

    // 连续不重叠（DFS 前序 = 节点 id 序 = 区间序）
    EXPECT_EQ(i0s + i0c, i1s);
    EXPECT_EQ(i1s + i1c, i2s);
    EXPECT_EQ(i2s + i2c, i3s);

    // net 区间：top 2 + mid 1 + bottom×2 各 1
    const auto [n0s, n0c] = tree.net_range(0);
    const auto [n1s, n1c] = tree.net_range(1);
    const auto [n2s, n2c] = tree.net_range(2);
    const auto [n3s, n3c] = tree.net_range(3);
    EXPECT_EQ(n0s, 0u);
    EXPECT_EQ(n0c, 2u);
    EXPECT_EQ(n1s, 2u);
    EXPECT_EQ(n1c, 1u);
    EXPECT_EQ(n2s, 3u);
    EXPECT_EQ(n2c, 1u);
    EXPECT_EQ(n3s, 4u);
    EXPECT_EQ(n3c, 1u);
    EXPECT_EQ(n0s + n0c, n1s);
    EXPECT_EQ(n1s + n1c, n2s);
    EXPECT_EQ(n2s + n2c, n3s);

    // via instance 区间（S5b 统计计数）：top 2 + mid 1 + bottom×2 各 0
    //（零长区间与后续区间共享 start，不消耗 id 空间）
    const auto [v0s, v0c] = tree.via_range(0);
    const auto [v1s, v1c] = tree.via_range(1);
    const auto [v2s, v2c] = tree.via_range(2);
    const auto [v3s, v3c] = tree.via_range(3);
    EXPECT_EQ(v0s, 0u);
    EXPECT_EQ(v0c, 2u);
    EXPECT_EQ(v1s, 2u);
    EXPECT_EQ(v1c, 1u);
    EXPECT_EQ(v2s, 3u);
    EXPECT_EQ(v2c, 0u);
    EXPECT_EQ(v3s, 3u);
    EXPECT_EQ(v3c, 0u);
    EXPECT_EQ(v0s + v0c, v1s);
    EXPECT_EQ(v1s + v1c, v2s);
}

// ── 3. 四接口之一：区间反查（三类含 via）────────────────────────────

TEST(DSHierTreeTest, BlockOfReverseLookup) {
    HierEnv env;
    HierPtrs ptrs(env);
    const DSHierTree tree = ds_build_hier_tree(ptrs.blocks, ptrs.nets,
                                               env.design);

    // instance 反查：根段 [0,4) → top；leaf 实例 1/2/3 全属 top（block
    // instance 自身的 global id（2 = mid）属父块——它就是父块内的实例）
    EXPECT_EQ(tree.block_of_instance(0), 0u);
    EXPECT_EQ(tree.block_of_instance(1), 0u);
    EXPECT_EQ(tree.block_of_instance(2), 0u);
    EXPECT_EQ(tree.block_of_instance(3), 0u);
    EXPECT_EQ(tree.block_of_instance(4), 1u);  // mid 占位槽
    EXPECT_EQ(tree.block_of_instance(5), 1u);
    EXPECT_EQ(tree.block_of_instance(6), 1u);
    EXPECT_EQ(tree.block_of_instance(7), 2u);
    EXPECT_EQ(tree.block_of_instance(9), 3u);
    EXPECT_EQ(tree.block_of_instance(11), DSDesign::kInvalidId);  // 越界

    // net 反查：[0,2) → top、[2,3) → mid、[3,4) → bottom#1、[4,5) → #2
    EXPECT_EQ(tree.block_of_net(0), 0u);
    EXPECT_EQ(tree.block_of_net(1), 0u);
    EXPECT_EQ(tree.block_of_net(2), 1u);
    EXPECT_EQ(tree.block_of_net(3), 2u);
    EXPECT_EQ(tree.block_of_net(4), 3u);
    EXPECT_EQ(tree.block_of_net(5), DSDesign::kInvalidId);

    // via 反查：[0,2) → top、[2,3) → mid；bottom 零长区间不含任何 id
    EXPECT_EQ(tree.block_of_via_instance(0), 0u);
    EXPECT_EQ(tree.block_of_via_instance(1), 0u);
    EXPECT_EQ(tree.block_of_via_instance(2), 1u);
    EXPECT_EQ(tree.block_of_via_instance(3), DSDesign::kInvalidId);
}

// ── 4. ⑧ local 0 映射 + global id 换算（⑨，S9 flatten 输入口）───────

TEST(DSHierTreeTest, GlobalIdConversionAndLocalZeroMapping) {
    HierEnv env;
    HierPtrs ptrs(env);
    const DSHierTree tree = ds_build_hier_tree(ptrs.blocks, ptrs.nets,
                                               env.design);

    // instance：local 0 = block 自身占位 → 该 block instance 的 global id
    EXPECT_EQ(tree.global_instance_id(0, 0), 0u);  // root → 0（⑧）
    EXPECT_EQ(tree.global_instance_id(1, 0), 2u);  // mid 自身
    EXPECT_EQ(tree.global_instance_id(2, 0), 5u);  // bottom#1 自身
    EXPECT_EQ(tree.global_instance_id(3, 0), 6u);  // bottom#2 自身
    // local ≥ 1 → start + local
    EXPECT_EQ(tree.global_instance_id(0, 1), 1u);
    EXPECT_EQ(tree.global_instance_id(0, 3), 3u);
    EXPECT_EQ(tree.global_instance_id(1, 1), 5u);  // bottom#1 从 mid 视角
    EXPECT_EQ(tree.global_instance_id(2, 1), 8u);  // bottom#1 的 leaf
    EXPECT_EQ(tree.global_instance_id(0, 4), DSDesign::kInvalidId);  // 越界

    // net：local id 从 1 起（0 保留未用），global = start + local − 1
    EXPECT_EQ(tree.global_net_id(0, 1), 0u);
    EXPECT_EQ(tree.global_net_id(0, 2), 1u);
    EXPECT_EQ(tree.global_net_id(1, 1), 2u);
    EXPECT_EQ(tree.global_net_id(2, 1), 3u);
    EXPECT_EQ(tree.global_net_id(3, 1), 4u);
    EXPECT_EQ(tree.global_net_id(0, 0), DSDesign::kInvalidId);  // 0 未用
    EXPECT_EQ(tree.global_net_id(1, 2), DSDesign::kInvalidId);  // 越界

    // via instance：同 net 语义（local 从 1 起 → start + local − 1）；
    // 零计数块（bottom）换算均无效
    EXPECT_EQ(tree.global_via_instance_id(0, 1), 0u);
    EXPECT_EQ(tree.global_via_instance_id(0, 2), 1u);
    EXPECT_EQ(tree.global_via_instance_id(1, 1), 2u);
    EXPECT_EQ(tree.global_via_instance_id(0, 0), DSDesign::kInvalidId);
    EXPECT_EQ(tree.global_via_instance_id(2, 1), DSDesign::kInvalidId);
    EXPECT_EQ(tree.global_via_instance_id(1, 2), DSDesign::kInvalidId);
}

// ── 5. format_tree：以 name 打印缩进层级文本 ────────────────────────

TEST(DSHierTreeTest, FormatsNamedIndentedTree) {
    HierEnv env;
    HierPtrs ptrs(env);
    const DSHierTree tree = ds_build_hier_tree(ptrs.blocks, ptrs.nets,
                                               env.design);
    const CMString text = tree.format_tree();

    // name 表示（block cell 名 + 实例名）+ 缩进层级
    EXPECT_NE(text.find("top as top"), CMString::npos);
    EXPECT_NE(text.find("  mid as i2"), CMString::npos);       // 一层缩进
    EXPECT_NE(text.find("    bottom as i1"), CMString::npos);  // 两层缩进
    EXPECT_NE(text.find("    bottom as i2"), CMString::npos);  // 同定义再次实例化
    // 区间表随行打印（三类）
    EXPECT_NE(text.find("inst=[0,4)"), CMString::npos);
    EXPECT_NE(text.find("via=[2,3)"), CMString::npos);
}

// ── 6. 序列化 round-trip（⑬ 挂 DSDesign 容器持久化能力锚定）─────────

TEST(DSHierTreeTest, SerializeRoundTrip) {
    HierEnv env;
    HierPtrs ptrs(env);
    DSHierTree tree = ds_build_hier_tree(ptrs.blocks, ptrs.nets, env.design);

    CMString blob;
    FLY_ENCODE(tree, blob);
    DSHierTree back;
    FLY_DECODE(blob, DSHierTree, back);

    EXPECT_EQ(back.get_design_name(), "top");
    ASSERT_EQ(back.node_count(), 4u);
    EXPECT_EQ(back.node(1).get_block_cell_name(), "mid");
    EXPECT_EQ(back.node(1).get_instance_name(), "i2");
    EXPECT_EQ(back.node(2).get_self_global_id(), 5u);
    EXPECT_EQ(back.instance_range(2).first, 7u);
    EXPECT_EQ(back.instance_range(2).second, 2u);
    EXPECT_EQ(back.net_range(3).first, 4u);
    EXPECT_EQ(back.via_range(1).first, 2u);
    EXPECT_EQ(back.via_range(1).second, 1u);
    // 反查与换算在往返后仍正确
    EXPECT_EQ(back.block_of_instance(8), 2u);
    EXPECT_EQ(back.block_of_via_instance(2), 1u);
    EXPECT_EQ(back.global_instance_id(3, 0), 6u);
    EXPECT_EQ(back.global_via_instance_id(1, 1), 2u);

    // 挂 DSDesign 容器随容器序列化（⑬）
    env.design.set_hier_tree(std::move(tree));
    CMString design_blob;
    FLY_ENCODE(env.design, design_blob);
    DSDesign design_back;
    FLY_DECODE(design_blob, DSDesign, design_back);
    ASSERT_EQ(design_back.get_hier_tree().node_count(), 4u);
    EXPECT_EQ(design_back.get_hier_tree().block_of_instance(5), 1u);
    EXPECT_EQ(design_back.get_hier_tree().block_of_via_instance(1), 0u);
}

// ── 7. 主 DEF 判定：多根 / 零根 / 环 / 入参不对齐 → fatal（D22/DSGN::0011）──
// 2026-09-12 裁定：DSGN::0011 场景由 raise 改 MSG_FATAL_EXIT（进程码 80 退出
// + master 联动）——断言方式同步从 EXPECT_THROW 改为 fork 子进程退出码 80
//（共享断言设施 fly::test::expect_fatal_exit_code；单测进程未绑定 fatal
// 分发 → 仅本地落盘后 _exit）。

TEST(DSHierTreeTest, MultiRootFatalsWithCode80) {
    HierEnv env;
    env.blocks.clear();
    env.nets.clear();
    env.blocks.push_back(make_block("a", {}, 1));  // 互不引用 → 两个根
    env.blocks.push_back(make_block("b", {}, 1));
    env.nets.push_back(make_net(0));
    env.nets.push_back(make_net(0));

    HierPtrs ptrs(env);
    fly::test::expect_fatal_exit_code(
        [&] { (void)ds_build_hier_tree(ptrs.blocks, ptrs.nets, env.design); }, 80);
}

TEST(DSHierTreeTest, CycleFatalsWithCode80) {
    HierEnv env;
    env.blocks.clear();
    env.nets.clear();
    // 补两个 block cell（名 = DEF DESIGN 名，S4 语义）造环引用
    DSCell loop;
    loop.set_name("LOOPA");
    loop.set_block_cell();
    env.design.add_cell(std::move(loop));
    DSCell loop_b;
    loop_b.set_name("LOOPB");
    loop_b.set_block_cell();
    env.design.add_cell(std::move(loop_b));

    const uint32_t loopa_id = env.design.cell_names_.get_id("LOOPA");
    const uint32_t loopb_id = env.design.cell_names_.get_id("LOOPB");
    // root → A → B → A（A 被引用两次仍唯一根 root；DFS 入环 → fatal）
    env.blocks.push_back(make_block("root", {loopa_id}, 1));
    env.blocks.push_back(make_block("LOOPA", {loopb_id}, 1));
    env.blocks.push_back(make_block("LOOPB", {loopa_id}, 1));
    env.nets.push_back(make_net(0));
    env.nets.push_back(make_net(0));
    env.nets.push_back(make_net(0));

    HierPtrs ptrs(env);
    fly::test::expect_fatal_exit_code(
        [&] { (void)ds_build_hier_tree(ptrs.blocks, ptrs.nets, env.design); }, 80);
}

TEST(DSHierTreeTest, MismatchedNetCountsFatalsWithCode80) {
    HierEnv env;
    HierPtrs ptrs(env);
    ptrs.nets.pop_back();  // nets 与 blocks 数量不一致 = 调用方契约错误
    fly::test::expect_fatal_exit_code(
        [&] { (void)ds_build_hier_tree(ptrs.blocks, ptrs.nets, env.design); }, 80);
}

TEST(DSHierTreeTest, EmptyDefsYieldEmptyTree) {
    HierEnv env;
    CMVector<const DSBlockBuildData*> empty_blocks;
    CMVector<const DSNetBuildData*> empty_nets;
    const DSHierTree tree =
        ds_build_hier_tree(empty_blocks, empty_nets, env.design);
    EXPECT_EQ(tree.node_count(), 0u);
    EXPECT_EQ(tree.block_of_instance(0), DSDesign::kInvalidId);
    EXPECT_TRUE(tree.format_tree().empty());
}

}  // namespace
