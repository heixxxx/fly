// S7 跨块连接归并（并查集）单测（2026-09-13 裁定，方案 design-db-plan.md
// §2.6 + S7 节）：
//   1. 局部收集：per-DEF 边集（父网连接 (子实例名, port 名) × 子网连接
//      ("PIN", port 名) 对接——S5b 名字形态，无 local 0 条目）+ port 网
//      local id 集；
//   2. 基本 union / 电气等价：子网连两 port 到不同父网 → 两父网同类；
//   3. 多层嵌套：孙网 → 子网 → 顶层网，root = 顶层（层级最高优先）；
//   4. root 规范：同级取最小 global id；
//   5. 两层不变式：root_of_[x] 的 root_of_ 恒自映射（含序列化往返后）；
//   6. internal net 绝不入表；悬空 port 网 root = 自身 + 计数；root 块
//      port 网（顶层引脚连接）不入悬空口径；
//   7. 同一 block 定义两次实例化：两组独立 global id 不互并。
// 构造：三层嵌套 block（top → mid ×2 → bottom）纯合成连接数据，树构建
// 复用 ds_build_hier_tree（同 ds_hier_test 的合成环境模式）。
#include <emir/design/cpp/ds_merge.h>
#include <emir/design/cpp/ds_types.h>
#include <emir/design/cpp/ds_union.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using namespace fly;

// 合成 per-DEF 实例产物：(实例名, cell id) 序列 + 网数（网名 n0..n{k-1}）
DSBlockBuildData make_block(
    const char* name,
    const CMVector<std::pair<CMString, uint32_t>>& instances,
    size_t net_count) {
    DSBlockBuildData b;
    b.init_placeholder(name, DSDesign::kInvalidId);
    for (const auto& [iname, cell_id] : instances) {
        DSInstance inst;
        inst.set_cell_id(cell_id);
        b.add_instance(std::move(inst), iname);
    }
    for (size_t i = 0; i < net_count; ++i) {
        b.register_net("n" + std::to_string(i));
    }
    return b;
}

// 合成 per-DEF 网内容产物：local net id → (实例名, pin 名) 连接序列
//（S5b 连接表名字形态——port 引用 instance_name = "PIN"）
DSNetBuildData make_nets(
    const char* block_name,
    const CMVector<std::pair<uint64_t,
                             CMVector<std::pair<CMString, CMString>>>>& conns) {
    DSNetBuildData n;
    n.set_block_name(block_name);
    for (const auto& [net_id, pairs] : conns) {
        for (const auto& [inst, pin] : pairs) {
            DSNetConnection c;
            c.set_instance_name(inst);
            c.set_pin_name(pin);
            n.add_connection(net_id, std::move(c));
        }
    }
    return n;
}

// 两层不变式：全表 find 恒一步（root_of_[x] 的 root_of_ 恒自映射）
void expect_two_layer_invariant(const DSNetUnion& u) {
    for (const auto& [member, root] : u.root_of_) {
        (void)member;
        const auto it = u.root_of_.find(root);
        ASSERT_TRUE(it != u.root_of_.end()) << "root missing self-map";
        EXPECT_EQ(it->second, root) << "two-layer invariant broken";
    }
}

// ── 主场景（三层嵌套 + 同定义两次实例化）────────────────────────────
//   def0 top  ：m1→mid、m2→mid；网 nt(1)/nx(2)/niso(3)
//     nt：(m1, PB) + (PIN, TP)——块实例连接 + 顶层引脚（不产生 union）
//     nx：(m2, PB)
//     niso：(PIN, TISO)——root 块 port 网（不入悬空口径）
//   def1 mid  ：b1→bottom；网 nm(1)：(PIN, PB) + (b1, PC)
//   def2 bottom：无实例；网 nb(1)：(PIN, PC) / ni(2)：(u9, ZZ)（internal）
//     / nd(3)：(PIN, PD)（悬空）
// 树（DFS 前序 = 节点 id）：#0 top → #1 mid#1 → #2 bottom#1、#3 mid#2 →
// #4 bottom#2；net 区间 [0,3)/[3,4)/[4,7)/[7,8)/[8,11)。
struct UnionEnv {
    DSDesign design;
    DSHierTree tree;
    DSBlockBuildData top, mid, bottom;
    DSNetBuildData top_nets, mid_nets, bottom_nets;

    UnionEnv() {
        DSCell leaf;
        leaf.set_name("leaf");
        design.add_cell(std::move(leaf));
        DSCell mid_cell;
        mid_cell.set_name("mid");
        mid_cell.set_block_cell();
        design.add_cell(std::move(mid_cell));
        DSCell bot;
        bot.set_name("bottom");
        bot.set_block_cell();
        design.add_cell(std::move(bot));
        const uint32_t mid_id = design.cell_names_.get_id("mid");
        const uint32_t bottom_id = design.cell_names_.get_id("bottom");

        top = make_block("top", {{"m1", mid_id}, {"m2", mid_id}}, 3);
        top_nets = make_nets("top", {
            {1, {{"m1", "PB"}, {"PIN", "TP"}}},
            {2, {{"m2", "PB"}}},
            {3, {{"PIN", "TISO"}}},
        });
        mid = make_block("mid", {{"b1", bottom_id}}, 1);
        mid_nets = make_nets("mid", {
            {1, {{"PIN", "PB"}, {"b1", "PC"}}},
        });
        bottom = make_block("bottom", {}, 3);
        bottom_nets = make_nets("bottom", {
            {1, {{"PIN", "PC"}}},
            {2, {{"u9", "ZZ"}}},
            {3, {{"PIN", "PD"}}},
        });

        CMVector<const DSBlockBuildData*> blocks = {&top, &mid, &bottom};
        CMVector<const DSNetBuildData*> nets = {&top_nets, &mid_nets,
                                                &bottom_nets};
        tree = ds_build_hier_tree(blocks, nets, design);
    }

    // global id 换算（树 API，防手算漂移）
    uint64_t g(uint32_t node_id, uint64_t local_net) const {
        return tree.global_net_id(node_id, local_net);
    }

    DSNetUnion build_union() const {
        DSNetUnionSlice top_slice =
            ds_collect_net_union_slice(tree, top_nets, {&mid_nets});
        DSNetUnionSlice mid_slice =
            ds_collect_net_union_slice(tree, mid_nets, {&bottom_nets});
        DSNetUnionSlice bottom_slice =
            ds_collect_net_union_slice(tree, bottom_nets, {});
        CMVector<const DSNetUnionSlice*> slices = {&top_slice, &mid_slice,
                                                   &bottom_slice};
        return ds_build_net_union(tree, slices);
    }
};

// ── 1. 局部收集：边集 + port 网 local id 集 ─────────────────────────

TEST(DSNetUnionTest, CollectsSliceEdgesAndPortNets) {
    UnionEnv env;

    // top（两实例化位置一次收集）：nt↔nm@mid#1、nx↔nm@mid#2
    const DSNetUnionSlice top_slice =
        ds_collect_net_union_slice(env.tree, env.top_nets, {&env.mid_nets});
    ASSERT_EQ(top_slice.edges_.size(), 2u);
    EXPECT_EQ(top_slice.edges_[0].net_a_,
              std::min(env.g(0, 1), env.g(1, 1)));  // 规范化 (min, max)
    EXPECT_EQ(top_slice.edges_[0].net_b_,
              std::max(env.g(0, 1), env.g(1, 1)));
    EXPECT_EQ(top_slice.edges_[1].net_a_,
              std::min(env.g(0, 2), env.g(3, 1)));
    EXPECT_EQ(top_slice.edges_[1].net_b_,
              std::max(env.g(0, 2), env.g(3, 1)));
    // port 网：nt(1)（块实例连接 + 顶层引脚）、niso(3)（仅顶层引脚）
    EXPECT_EQ(top_slice.port_net_ids_, (CMVector<uint64_t>{1, 3}));
    EXPECT_EQ(top_slice.block_name_, "top");

    // mid：nm@mid#1↔nb@bottom#1、nm@mid#2↔nb@bottom#2
    const DSNetUnionSlice mid_slice = ds_collect_net_union_slice(
        env.tree, env.mid_nets, {&env.bottom_nets});
    ASSERT_EQ(mid_slice.edges_.size(), 2u);
    EXPECT_EQ(mid_slice.edges_[0].net_a_,
              std::min(env.g(1, 1), env.g(2, 1)));
    EXPECT_EQ(mid_slice.edges_[0].net_b_,
              std::max(env.g(1, 1), env.g(2, 1)));
    EXPECT_EQ(mid_slice.edges_[1].net_a_,
              std::min(env.g(3, 1), env.g(4, 1)));
    EXPECT_EQ(mid_slice.edges_[1].net_b_,
              std::max(env.g(3, 1), env.g(4, 1)));
    EXPECT_EQ(mid_slice.port_net_ids_, (CMVector<uint64_t>{1}));

    // bottom（叶块，无块实例连接）：无边；port 网 nb(1)/nd(3)，internal
    // ni(2) 不在 port 集名形态上（无 ("PIN", x) 引用）
    const DSNetUnionSlice bottom_slice =
        ds_collect_net_union_slice(env.tree, env.bottom_nets, {});
    EXPECT_TRUE(bottom_slice.edges_.empty());
    EXPECT_EQ(bottom_slice.port_net_ids_, (CMVector<uint64_t>{1, 3}));
    EXPECT_EQ(bottom_slice.block_name_, "bottom");
}

// ── 2. 基本 union + root 规范（层级最高优先、同级最小 global id）────

TEST(DSNetUnionTest, MergesEquivalentParentNetsWithCanonicalRoot) {
    DSDesign design;
    DSCell cb;
    cb.set_name("cb");
    cb.set_block_cell();
    design.add_cell(std::move(cb));
    const uint32_t cb_id = design.cell_names_.get_id("cb");

    // top：c1→cb；网 na(1)：(c1, P1)、nb(2)：(c1, P2)
    // cb ：网 nc(1)：(PIN, P1) + (PIN, P2)——一子网连两 port → 两父网
    // 电气等价（合法形态）
    DSBlockBuildData t2 = make_block("t2", {{"c1", cb_id}}, 2);
    DSBlockBuildData cbd = make_block("cb", {}, 1);
    DSNetBuildData t2_nets = make_nets("t2", {
        {1, {{"c1", "P1"}}},
        {2, {{"c1", "P2"}}},
    });
    DSNetBuildData cb_nets = make_nets("cb", {
        {1, {{"PIN", "P1"}, {"PIN", "P2"}}},
    });
    CMVector<const DSBlockBuildData*> blocks = {&t2, &cbd};
    CMVector<const DSNetBuildData*> nets = {&t2_nets, &cb_nets};
    const DSHierTree tree = ds_build_hier_tree(blocks, nets, design);

    DSNetUnionSlice t2_slice =
        ds_collect_net_union_slice(tree, t2_nets, {&cb_nets});
    DSNetUnionSlice cb_slice = ds_collect_net_union_slice(tree, cb_nets, {});
    // cb（子侧视角）无块实例连接 → 无边
    EXPECT_TRUE(cb_slice.edges_.empty());
    CMVector<const DSNetUnionSlice*> slices = {&t2_slice, &cb_slice};
    const DSNetUnion u = ds_build_net_union(tree, slices);

    // net 区间：t2 [0,2)、cb [2,3) → na=0、nb=1、nc=2
    const uint64_t na = tree.global_net_id(0, 1);
    const uint64_t nb = tree.global_net_id(0, 2);
    const uint64_t nc = tree.global_net_id(1, 1);

    // 基本 union：子网 root = 父网；两父网同类（电气等价）
    EXPECT_EQ(u.find(nc), na);
    EXPECT_EQ(u.find(nb), na);
    EXPECT_EQ(u.find(na), na);
    // root 规范：na/nb 同级（root 块 depth 0）取最小 global id；nc 层级
    // 更低落选
    ASSERT_NE(u.members(na), nullptr);
    EXPECT_EQ(*u.members(na), (CMVector<uint64_t>{na, nb, nc}));
    EXPECT_EQ(u.class_count(), 1u);
    EXPECT_EQ(u.dangling_count_, 0u);
    expect_two_layer_invariant(u);
}

// ── 3. 多层嵌套：孙网 → 子网 → 顶层网，root = 顶层 ──────────────────

TEST(DSNetUnionTest, ThreeLevelNestingRootAtTop) {
    UnionEnv env;
    const DSNetUnion u = env.build_union();

    // nt(0) ← nm@mid#1(3) ← nb@bottom#1(4)：跨三层归并 root = 顶层网
    EXPECT_EQ(u.find(env.g(1, 1)), env.g(0, 1));
    EXPECT_EQ(u.find(env.g(2, 1)), env.g(0, 1));
    ASSERT_NE(u.members(env.g(0, 1)), nullptr);
    EXPECT_EQ(*u.members(env.g(0, 1)),
              (CMVector<uint64_t>{env.g(0, 1), env.g(1, 1), env.g(2, 1)}));

    // nx(1) ← nm@mid#2(7) ← nb@bottom#2(8)：第二实例化位置的独立类
    EXPECT_EQ(u.find(env.g(3, 1)), env.g(0, 2));
    EXPECT_EQ(u.find(env.g(4, 1)), env.g(0, 2));
    EXPECT_EQ(*u.members(env.g(0, 2)),
              (CMVector<uint64_t>{env.g(0, 2), env.g(3, 1), env.g(4, 1)}));

    expect_two_layer_invariant(u);
}

// ── 4. 同一定义两次实例化：两组独立 global id 不互并 ────────────────

TEST(DSNetUnionTest, TwoInstantiationsStaySeparate) {
    UnionEnv env;
    const DSNetUnion u = env.build_union();

    // mid#1 的 nm 与 mid#2 的 nm 是不同 global id，分属两类
    const uint64_t nm1 = env.g(1, 1);
    const uint64_t nm2 = env.g(3, 1);
    EXPECT_NE(nm1, nm2);
    EXPECT_NE(u.find(nm1), u.find(nm2));
    // 成员枚举互不串类
    const CMVector<uint64_t>& m1 = *u.members(u.find(nm1));
    const CMVector<uint64_t>& m2 = *u.members(u.find(nm2));
    EXPECT_EQ(std::find(m1.begin(), m1.end(), nm2), m1.end());
    EXPECT_EQ(std::find(m2.begin(), m2.end(), nm1), m2.end());
}

// ── 5. internal net 不入表；悬空 port root = 自身 + 计数 ────────────

TEST(DSNetUnionTest, InternalNetExcludedDanglingPortCounted) {
    UnionEnv env;
    const DSNetUnion u = env.build_union();

    // internal net ni(5)：无 ("PIN", x) 引用 → 绝不入表（红线），find = 自身
    const uint64_t ni = env.g(2, 2);
    EXPECT_EQ(u.find(ni), ni);
    EXPECT_EQ(u.members(ni), nullptr);

    // 悬空 port 网 nd：port PD 未连接任何父网 → 照常入表 root = 自身
    //（两个实例化位置各自独立悬空）+ 计数
    const uint64_t nd1 = env.g(2, 3);
    const uint64_t nd2 = env.g(4, 3);
    EXPECT_EQ(u.find(nd1), nd1);
    EXPECT_EQ(*u.members(nd1), (CMVector<uint64_t>{nd1}));
    EXPECT_EQ(u.find(nd2), nd2);
    EXPECT_EQ(*u.members(nd2), (CMVector<uint64_t>{nd2}));
    EXPECT_EQ(u.dangling_count_, 2u);
    EXPECT_EQ(u.class_count(), 4u);  // 2 个跨块类 + 2 个悬空单成员类

    // root 块 port 网 niso(2)：顶层引脚连接，root 候选语义——不入表、
    // 不入悬空口径
    const uint64_t niso = env.g(0, 3);
    EXPECT_EQ(u.find(niso), niso);
    EXPECT_EQ(u.members(niso), nullptr);
}

// ── 6. 两层不变式：全部路径（含序列化往返后）成立 ───────────────────

TEST(DSNetUnionTest, TwoLayerInvariantHoldsAndRoundTrips) {
    UnionEnv env;
    DSNetUnion u = env.build_union();
    expect_two_layer_invariant(u);

    // 序列化往返（S7 正式对象独立落盘能力锚定）
    CMString blob;
    FLY_ENCODE(u, blob);
    DSNetUnion back;
    FLY_DECODE(blob, DSNetUnion, back);

    EXPECT_EQ(back.dangling_count_, u.dangling_count_);
    EXPECT_EQ(back.class_count(), u.class_count());
    for (const auto& [member, root] : u.root_of_) {
        EXPECT_EQ(back.find(member), root);
    }
    for (const auto& [root, members] : u.members_of_) {
        const CMVector<uint64_t>* m = back.members(root);
        ASSERT_NE(m, nullptr);
        EXPECT_EQ(*m, members);
    }
    expect_two_layer_invariant(back);
}

// ── 7. 空输入兜底：无树/无边 → 空结果放行（dev-rules §7）────────────

TEST(DSNetUnionTest, EmptyInputsYieldEmptyUnion) {
    DSHierTree empty_tree;
    CMVector<const DSNetUnionSlice*> no_slices;
    const DSNetUnion u = ds_build_net_union(empty_tree, no_slices);
    EXPECT_EQ(u.class_count(), 0u);
    EXPECT_EQ(u.dangling_count_, 0u);
    EXPECT_EQ(u.find(42u), 42u);  // 不在表 = 自身
    EXPECT_EQ(u.members(42u), nullptr);

    // 有树无边（无跨块连接的设计）同样空结果
    UnionEnv env;
    DSNetUnionSlice top_slice =
        ds_collect_net_union_slice(env.tree, env.top_nets, {});
    CMVector<const DSNetUnionSlice*> slices = {&top_slice};
    const DSNetUnion u2 = ds_build_net_union(env.tree, slices);
    // top 的边全部需要 mid 网产物对接——未提供子定义 → 无边；port 网
    //（nt/niso 均为 root 块 port 网）不入悬空口径 → 空结果
    EXPECT_EQ(u2.class_count(), 0u);
    EXPECT_EQ(u2.dangling_count_, 0u);
}

TEST(DSNetUnionTest, NoEdgesStillCountsDanglingPorts) {
    // review 2026-09-13 修复锁定：无边（无跨块连接）不跳过悬空判定——
    // 非 root 块的 port 网照常入表（root=自身）+ 计数。原实现的
    // parent.empty() 提前返回使「全部 port 未连父网」输入形态下悬空
    // 整体丢失（class_count=0 / dangling=0，reviewer 运行证实）
    UnionEnv env;
    DSNetUnionSlice mid_slice =
        ds_collect_net_union_slice(env.tree, env.mid_nets, {});
    DSNetUnionSlice bottom_slice =
        ds_collect_net_union_slice(env.tree, env.bottom_nets, {});
    CMVector<const DSNetUnionSlice*> slices = {&mid_slice, &bottom_slice};
    const DSNetUnion u = ds_build_net_union(env.tree, slices);
    // 无边（mid 的边需 top 侧、bottom 的边需 mid 侧——均未提供）；树形态
    // = top→m1/m2（mid ×2 实例化）→ 每mid 实例化 bottom ×1（bottom ×2
    // 实例化）：mid PB ×2 位置 + bottom PC/PD ×2 位置 = 6 个悬空单成员类
    EXPECT_EQ(u.class_count(), 6u);
    EXPECT_EQ(u.dangling_count_, 6u);
    // 悬空 root=自身（两层不变式；抽查 mid 首位置与 bottom 首位置）
    uint32_t mid_node = env.tree.node_count();
    uint32_t bottom_node = env.tree.node_count();
    for (uint32_t i = 0; i < env.tree.node_count(); ++i) {
        const CMString name = env.tree.node(i).get_block_cell_name();
        if (name == "mid" && mid_node == env.tree.node_count()) mid_node = i;
        if (name == "bottom" && bottom_node == env.tree.node_count()) {
            bottom_node = i;
        }
    }
    ASSERT_LT(mid_node, env.tree.node_count());
    ASSERT_LT(bottom_node, env.tree.node_count());
    EXPECT_EQ(u.find(env.g(mid_node, 1)), env.g(mid_node, 1));        // PB
    EXPECT_EQ(u.find(env.g(bottom_node, 1)), env.g(bottom_node, 1));  // PC
    EXPECT_EQ(u.find(env.g(bottom_node, 3)), env.g(bottom_node, 3));  // PD
    // internal 网（bottom 的 ni）不入表
    EXPECT_EQ(u.find(env.g(bottom_node, 2)), env.g(bottom_node, 2));
    EXPECT_EQ(u.members(env.g(bottom_node, 2)), nullptr);
}

// ── 8. 编排辅助：def 序号 → 子定义序号集 ────────────────────────────

TEST(DSNetUnionTest, ChildIndexesFromTree) {
    UnionEnv env;
    // def_paths 序 = [top, mid, bottom]（UnionEnv 构造序）
    EXPECT_EQ(ds_net_union_child_indexes(env.tree, {"top", "mid", "bottom"}, 0),
              (CMVector<uint32_t>{1}));  // top → mid
    EXPECT_EQ(ds_net_union_child_indexes(env.tree, {"top", "mid", "bottom"}, 1),
              (CMVector<uint32_t>{2}));  // mid → bottom
    EXPECT_TRUE(ds_net_union_child_indexes(
        env.tree, {"top", "mid", "bottom"}, 2)
                    .empty());  // bottom 为叶
    // 未在树上出现的 def（无位置）→ 空集
    EXPECT_TRUE(ds_net_union_child_indexes(env.tree, {"top", "mid", "bottom"},
                                           9)
                    .empty());
}

}  // namespace
