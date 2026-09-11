// DSNameMapperT 单测（R7 全局组装层，裁定 ㊵①㊹㊻；方案
// design-db-phase2-plan.md §0 ㊹/㊻ 行）：
//   1. get_global_id：拆路径（'/' 分隔）→ 自根按实例名逐层定位
//      DSHierNode → 叶层 hasher get_id → local id + 区间 start
//      （⑨ 换算；instance local 0 → self_global_id ⑧、net local 从
//      1 起 start + local − 1）；
//   2. get_full_name：区间反查节点 → 叶层 hasher get_name → 递归向上
//      拼 block instance 名 prefix——与 get_global_id 双向闭环；
//   3. 多层嵌套（含同一 block 定义多次实例化——bottom ×2）；
//   4. 未注入 block 的查询返回哨兵/空名（㊻ 局部注入 = 局部可查）；
//   5. 注入式轻壳（㊻）：set_block_hasher 按 cell id（主口）/ cell name
//      （便利口，经树解析）注入；mapper 不序列化 hasher（编译面无
//      FLY_SERIALIZE）；
//   6. 两维度（DSInstanceNameMapper/DSNetNameMapper = <uint64_t>，㊹）
//      同一模板实例化、维度运行时区分——net 的区间换算与 instance 不同。
// 构造：三层嵌套（top → mid ×1 → bottom ×2）纯合成，hasher 手工登记。
#include <emir/design/cpp/ds_name_hasher.h>
#include <emir/design/cpp/ds_name_mapper.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace {

using namespace fly;

// 伴生对象（S5a 产物语义）：instance/net local id 从 1 起。以共享指针
// 持有（注入 DSNameMapperT 即共享其 hasher——零拷贝）。
struct LeafHashers {
    CMSharedPtr<DSBlockNames> names = CMMakeShared<DSBlockNames>();
    DSInstanceNameHasher& inst = *names->instance_names_;
    DSNetNameHasher& net = *names->net_names_;
};

// 测试层级（节点区间与 ds_hier_test 的标准场景同构）：
//   #0 top   (self 0, inst [0,4),  net [0,2))  local: i1/i2/i3
//    └ #1 mid  (self 2, inst [4,7),  net [2,3))  local: i1/i2（bottom ×2）
//        ├ #2 bottom#1 (self 5, inst [7,9),  net [3,4))  local: i1
//        └ #3 bottom#2 (self 6, inst [9,11), net [4,5))  local: i1
struct MapperEnv {
    DSHierTree tree;
    LeafHashers top;
    LeafHashers mid;
    LeafHashers bottom;  // 同一定义（bottom）两实例共用一份 hasher

    MapperEnv() {
        // 手工合成树（与 ds_build_hier_tree 产物的区间形态一致）
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

        DSHierNode mid_node;
        mid_node.id_ = 1;
        mid_node.parent_id_ = 0;
        mid_node.block_cell_name_ = "mid";
        mid_node.instance_name_ = "i2";
        mid_node.self_global_id_ = 2;
        mid_node.instance_start_ = 4;
        mid_node.instance_count_ = 3;
        mid_node.net_start_ = 2;
        mid_node.net_count_ = 1;
        mid_node.via_start_ = 0;
        mid_node.via_count_ = 0;

        DSHierNode b1;
        b1.id_ = 2;
        b1.parent_id_ = 1;
        b1.block_cell_name_ = "bottom";
        b1.instance_name_ = "i1";
        b1.self_global_id_ = 5;
        b1.instance_start_ = 7;
        b1.instance_count_ = 2;
        b1.net_start_ = 3;
        b1.net_count_ = 1;

        DSHierNode b2;
        b2.id_ = 3;
        b2.parent_id_ = 1;
        b2.block_cell_name_ = "bottom";
        b2.instance_name_ = "i2";
        b2.self_global_id_ = 6;
        b2.instance_start_ = 9;
        b2.instance_count_ = 2;
        b2.net_start_ = 4;
        b2.net_count_ = 1;

        tree.nodes_.push_back(std::move(root));
        tree.nodes_.push_back(std::move(mid_node));
        tree.nodes_.push_back(std::move(b1));
        tree.nodes_.push_back(std::move(b2));
        tree.nodes_[0].get_ref_children_ids().push_back(1);
        tree.nodes_[1].get_ref_children_ids().push_back(2);
        tree.nodes_[1].get_ref_children_ids().push_back(3);
        tree.design_name_ = "top";

        // hasher 登记（S5a 语义：local id 从 1 起）
        top.inst.assign("i1", 1);
        top.inst.assign("i2", 2);
        top.inst.assign("i3", 3);
        top.net.assign("n0", 1);
        top.net.assign("n1", 2);
        mid.inst.assign("i1", 1);
        mid.inst.assign("i2", 2);
        mid.net.assign("n0", 1);
        bottom.inst.assign("i1", 1);
        bottom.net.assign("n0", 1);
    }
};

// 挂 block_cell_id（cell id → hasher 注入表的键；真实产物由
// ds_build_hier_tree 填充，测试手工对齐：top=0/mid=1/bottom=2）
void attach_cell_ids(MapperEnv& env) {
    env.tree.nodes_[0].block_cell_id_ = 0;
    env.tree.nodes_[1].block_cell_id_ = 1;
    env.tree.nodes_[2].block_cell_id_ = 2;
    env.tree.nodes_[3].block_cell_id_ = 2;
}

// 全量注入的 instance mapper
DSInstanceNameMapper make_full_instance_mapper(MapperEnv& env) {
    attach_cell_ids(env);
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    // hasher 级共享注入：CMSharedPtr const 化（零拷贝零 move）
    mapper.set_block_hasher(0, env.top.names->instance_names_);
    mapper.set_block_hasher(1, env.mid.names->instance_names_);
    mapper.set_block_hasher(2, env.bottom.names->instance_names_);
    return mapper;
}

// ── 1. get_global_id：层级路径 → global id（⑨ 换算 + ⑧ local 0）────

TEST(DSNameMapperTest, GetGlobalIdResolvesHierarchyPaths) {
    MapperEnv env;
    const DSInstanceNameMapper mapper = make_full_instance_mapper(env);

    // 叶层实例（叶层 hasher local + 区间 start）
    EXPECT_EQ(mapper.get_global_id("top/i1"), 1u);
    EXPECT_EQ(mapper.get_global_id("top/i3"), 3u);
    // block instance 本身：父块 hasher 中登记的 local id（= 节点
    // self_global_id，两路等价）
    EXPECT_EQ(mapper.get_global_id("top/i2"), 2u);
    // 两层嵌套（mid 内实例）
    EXPECT_EQ(mapper.get_global_id("top/i2/i1"), 5u);
    EXPECT_EQ(mapper.get_global_id("top/i2/i2"), 6u);
    // 三层嵌套（bottom 内 leaf）
    EXPECT_EQ(mapper.get_global_id("top/i2/i1/i1"), 8u);
    // 同一 block 定义多次实例化：各自区间独立换算
    EXPECT_EQ(mapper.get_global_id("top/i2/i2/i1"), 10u);

    // 未命中场景（㊴ 哨兵）
    EXPECT_EQ(mapper.get_global_id("top/ghost"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_global_id("top/i2/ghost"),
              DSInstanceNameMapper::kInvalidId);
    // root 段不匹配 / 路径断裂 / 空路径
    EXPECT_EQ(mapper.get_global_id("ghost/i1"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_global_id("top/i1/deeper"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_global_id("top"), DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_global_id(""), DSInstanceNameMapper::kInvalidId);
}

// ── 2. get_full_name：global id → 层级路径（含 local 0 → self 路径）─

TEST(DSNameMapperTest, GetFullNameBuildsHierarchyPrefix) {
    MapperEnv env;
    const DSInstanceNameMapper mapper = make_full_instance_mapper(env);

    // root local 0 → 自身实例名路径（⑧ root global 0 = top）
    EXPECT_EQ(mapper.get_full_name(0), "top");
    // block instance 自身（self_global_id 落在父块区间）→ 自身路径
    EXPECT_EQ(mapper.get_full_name(2), "top/i2");
    EXPECT_EQ(mapper.get_full_name(5), "top/i2/i1");
    EXPECT_EQ(mapper.get_full_name(6), "top/i2/i2");
    // 叶实例 → prefix + 实例名
    EXPECT_EQ(mapper.get_full_name(1), "top/i1");
    EXPECT_EQ(mapper.get_full_name(3), "top/i3");
    EXPECT_EQ(mapper.get_full_name(8), "top/i2/i1/i1");
    EXPECT_EQ(mapper.get_full_name(10), "top/i2/i2/i1");

    // 未命中（越界区间反查）→ 空名
    EXPECT_EQ(mapper.get_full_name(11), "");
    EXPECT_EQ(mapper.get_full_name(DSInstanceNameMapper::kInvalidId), "");
}

TEST(DSNameMapperTest, GlobalIdAndFullNameRoundTrip) {
    MapperEnv env;
    const DSInstanceNameMapper mapper = make_full_instance_mapper(env);

    // 双向闭环：登记过的每个 global id，name → id → name 恒等
    const CMVector<CMString> paths = {
        "top/i1", "top/i2", "top/i3",  "top/i2/i1",
        "top/i2/i2", "top/i2/i1/i1", "top/i2/i2/i1",
    };
    for (const CMString& p : paths) {
        const uint64_t gid = mapper.get_global_id(p);
        ASSERT_TRUE(DSInstanceNameMapper::is_valid_id(gid)) << p;
        EXPECT_EQ(mapper.get_full_name(gid), p) << p;
    }
}

// ── 3. 未注入 block（㊻ 局部注入 = 局部可查）────────────────────────

TEST(DSNameMapperTest, PartialInjectionQueries) {
    MapperEnv env;
    attach_cell_ids(env);
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    mapper.set_block_hasher(0, env.top.names->instance_names_);  // 仅注入 top

    // top 域内可查
    EXPECT_EQ(mapper.get_global_id("top/i1"), 1u);
    EXPECT_EQ(mapper.get_full_name(1), "top/i1");
    EXPECT_EQ(mapper.get_full_name(2), "top/i2");  // self 路径不经 hasher
    // mid/bottom 域未注入 → 哨兵 / 空名
    EXPECT_EQ(mapper.get_global_id("top/i2/i1"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_global_id("top/i2/i1/i1"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_full_name(5), "");
    EXPECT_EQ(mapper.get_full_name(8), "");
}

TEST(DSNameMapperTest, SetBlockHasherByCellName) {
    // 便利口：cell name 经树解析（block_cell_name_ → block_cell_id_）
    MapperEnv env;
    attach_cell_ids(env);
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    mapper.set_block_hasher("top", env.top.names->instance_names_);
    mapper.set_block_hasher("bottom", env.bottom.names->instance_names_);

    EXPECT_EQ(mapper.get_global_id("top/i1"), 1u);
    EXPECT_EQ(mapper.get_global_id("top/i2/i1/i1"), 8u);
    // mid 未注入：mid 域全部未命中（含 block instance 自身的名字 i2——
    // ㊻ 局部注入 = 局部可查；同名 bottom 节点 ×2 共享同一份 hasher，
    // 注入按 block 定义一次覆盖全部实例）
    EXPECT_EQ(mapper.get_global_id("top/i2/i1"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_global_id("top/i2/i2"),
              DSInstanceNameMapper::kInvalidId);
    // 未知 cell name 便利口：不注册、不生效
    mapper.set_block_hasher("ghost_block", env.top.names->instance_names_);
    EXPECT_EQ(mapper.injected_count(), 2u);
}

// ── 4. net 维度（区间换算 start + local − 1，local 从 1 起）─────────

TEST(DSNameMapperTest, NetDimensionMapping) {
    MapperEnv env;
    attach_cell_ids(env);
    DSNetNameMapper mapper(&env.tree, DSNameMapperKind::NET);
    mapper.set_block_hasher(0, env.top.names->net_names_);
    mapper.set_block_hasher(1, env.mid.names->net_names_);
    mapper.set_block_hasher(2, env.bottom.names->net_names_);

    // top net [0,2)：n0 → local 1 → 0 + 1 − 1 = 0；n1 → 1
    EXPECT_EQ(mapper.get_global_id("top/n0"), 0u);
    EXPECT_EQ(mapper.get_global_id("top/n1"), 1u);
    // mid net [2,3)；bottom#1 net [3,4)、bottom#2 net [4,5)
    EXPECT_EQ(mapper.get_global_id("top/i2/n0"), 2u);
    EXPECT_EQ(mapper.get_global_id("top/i2/i1/n0"), 3u);
    EXPECT_EQ(mapper.get_global_id("top/i2/i2/n0"), 4u);

    // 反向（含嵌套 prefix）
    EXPECT_EQ(mapper.get_full_name(0), "top/n0");
    EXPECT_EQ(mapper.get_full_name(2), "top/i2/n0");
    EXPECT_EQ(mapper.get_full_name(4), "top/i2/i2/n0");
    EXPECT_EQ(mapper.get_full_name(5), "");  // 越界

    // net 无 local 0（保留未用）：换算不可达路径防御
    EXPECT_EQ(mapper.get_global_id("top/ghost"),
              DSNetNameMapper::kInvalidId);
}

// ── 5. 注入式轻壳（㊻）：不序列化 hasher、运行时构造 ─────────────────

// SFINAE 探测 FLY_SERIALIZE 生成的 fly_serialize(std::ostream&) 成员
// （非模板成员，探测安全；mapper 编译面 = 无序列化成员）
template <typename T, typename = void>
struct has_serialize : std::false_type {};
template <typename T>
struct has_serialize<
    T, std::void_t<decltype(std::declval<const T&>().fly_serialize(
           std::declval<std::ostream&>()))>> : std::true_type {};

static_assert(!has_serialize<DSInstanceNameMapper>::value,
              "DSNameMapperT must not be serializable (㊻ 注入式轻壳)");
static_assert(!has_serialize<DSNetNameMapper>::value,
              "DSNameMapperT must not be serializable (㊻ 注入式轻壳)");
static_assert(has_serialize<DSBlockNames>::value,
              "DSBlockNames carries the hashers and serializes (㊵②)");

TEST(DSNameMapperTest, MapperIsLightweightShell) {
    // ㊻：mapper 自身不保存 hasher 所有权（注入表存非拥有观察指针），
    // FLY_SERIALIZE 缺席——运行时构造、不落盘（static_assert 见文件头）。
    MapperEnv env;
    attach_cell_ids(env);
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    mapper.set_block_hasher(0, env.top.names->instance_names_);
    EXPECT_EQ(mapper.injected_count(), 1u);
    // 重复注入同键覆盖（指向更新）：cell id 0 的 hasher 换成 mid 定义
    // 的——mid.inst 无 "i3"，覆盖后原先可查的 top 域 leaf 失效
    mapper.set_block_hasher(0, env.mid.names->instance_names_);
    EXPECT_EQ(mapper.injected_count(), 1u);
    EXPECT_EQ(mapper.get_global_id("top/i3"),
              DSInstanceNameMapper::kInvalidId);
}

TEST(DSNameMapperTest, EmptyTreeAndDefaults) {
    MapperEnv env;
    attach_cell_ids(env);
    // 无树（默认构造）：一切查询未命中
    DSInstanceNameMapper bare;
    EXPECT_EQ(bare.get_global_id("top/i1"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(bare.get_full_name(0), "");
    // 有树未注入：一切查询未命中（任务书裁定：叶层只走注入 hasher，
    // 树仅提供层级结构——树自身无 leaf 名空间；local 0 的路径反查除外，
    // ⑧ 结构性占位）
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    EXPECT_EQ(mapper.get_global_id("top/i1"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_full_name(2), "");
    EXPECT_EQ(mapper.get_full_name(1), "");
}

// ── 6. 分派索引（R8c，裁定 53 方案 B）：正确性专项 ────────────────────
//
// get_global_id 的分派已从逐层 find_child_by_instance_name 线性扫收编
// 为内建索引（block 层次全路径 → 树节点 id，DSHasherBackendHatrie
// <uint32_t> 直接实例）——本节锁定索引与原逐层下降的语义等价：
// 多层多扇出抽样、同名兄弟防御（保留首个）、未注入叶层、非法路径、
// set_tree 换树重建。

// 分派索引专项层级（合成）：depth 3 × 叶层扇出 50——
//   #0 top（inst [0, 1+50×(N+1))，此处 N=4）
//    └ #1 m1（主链）
//        └ #2..#51 leaf_<k>（同一定义 cell id 7，共享叶 hasher：
//          local 1..4 = f0/f1/f2/f3）
struct DispatchEnv {
    DSHierTree tree;
    CMSharedPtr<DSBlockNames> leaf_names = CMMakeShared<DSBlockNames>();

    static constexpr uint32_t kLeafCellId = 7;
    static constexpr uint64_t kLeafLocalCount = 4;  // 叶名 local 1..4

    DispatchEnv() {
        const int fanout = 50;
        tree.nodes_.resize(2 + static_cast<size_t>(fanout));
        for (size_t i = 0; i < tree.nodes_.size(); ++i) {
            tree.nodes_[i].id_ = static_cast<uint32_t>(i);
            tree.nodes_[i].parent_id_ = static_cast<uint32_t>(i);
        }
        DSHierNode& root = tree.nodes_[0];
        root.block_cell_name_ = "top";
        root.instance_name_ = "top";
        DSHierNode& mid = tree.nodes_[1];
        mid.parent_id_ = 0;
        mid.block_cell_name_ = "mid";
        mid.instance_name_ = "m1";
        tree.nodes_[0].get_ref_children_ids().push_back(1);
        // 区间分配（DFS 前序连续；叶区间 = 自身占位 + kLeafLocalCount）
        uint64_t cursor = 0;
        cursor += 1;  // root 自身占位
        cursor += 1;  // m1 自身占位
        for (int k = 0; k < fanout; ++k) {
            DSHierNode& leaf = tree.nodes_[2 + static_cast<size_t>(k)];
            leaf.parent_id_ = 1;
            leaf.block_cell_name_ = "leafcell";
            leaf.instance_name_ = "leaf_" + std::to_string(k);
            leaf.block_cell_id_ = kLeafCellId;
            leaf.self_global_id_ = 1 + static_cast<uint64_t>(k) + 1;
            leaf.instance_start_ = cursor;
            leaf.instance_count_ = 1 + kLeafLocalCount;
            cursor += leaf.instance_count_;
            tree.nodes_[1].get_ref_children_ids().push_back(leaf.id_);
        }
        root.self_global_id_ = 0;
        mid.self_global_id_ = 1;
        root.instance_start_ = 0;
        root.instance_count_ = cursor;
        mid.instance_start_ = 1;
        mid.instance_count_ = cursor - 1;
        tree.design_name_ = "top";
        // 叶 hasher 登记（local 1..4；叶区间起点 = 2 + k×(1+4) + 1）
        leaf_names->instance_names_->assign("f0", 1);
        leaf_names->instance_names_->assign("f1", 2);
        leaf_names->instance_names_->assign("f2", 3);
        leaf_names->instance_names_->assign("f3", 4);
    }

    // 叶 k 的叶名 local l 的期望 global id（区间换算独立于 mapper 计算）
    uint64_t expect_id(int leaf_k, uint64_t local) const {
        const DSHierNode& leaf = tree.nodes_[2 + static_cast<size_t>(leaf_k)];
        return leaf.instance_start_ + local;
    }
};

TEST(DSNameMapperDispatchTest, MultiLayerMultiFanoutMatchesExpectation) {
    DispatchEnv env;
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    mapper.set_block_hasher(DispatchEnv::kLeafCellId, env.leaf_names->instance_names_);

    // 全叶节点 × 全叶名逐条对齐独立计算的期望 id（50 × 4 = 200 条全量）
    for (int k = 0; k < 50; ++k) {
        const CMString prefix =
            "top/m1/leaf_" + std::to_string(k) + "/";
        for (uint64_t local = 1; local <= DispatchEnv::kLeafLocalCount;
             ++local) {
            const CMString path = prefix + "f" + std::to_string(local - 1);
            EXPECT_EQ(mapper.get_global_id(path), env.expect_id(k, local))
                << path;
        }
    }
    // block instance 自身路径（两路等价：父块 hasher 未注入 → 分派命中
    // 叶节点后叶段走叶 hasher；此处查的是叶节点自身名作叶段的场景——
    // m1 未注入 → 哨兵）
    EXPECT_EQ(mapper.get_global_id("top/m1"),
              DSInstanceNameMapper::kInvalidId);
    // 双向闭环抽样
    for (int k : {0, 17, 49}) {
        for (uint64_t local = 1; local <= DispatchEnv::kLeafLocalCount;
             ++local) {
            const uint64_t gid = env.expect_id(k, local);
            const CMString path =
                "top/m1/leaf_" + std::to_string(k) + "/f" +
                std::to_string(local - 1);
            EXPECT_EQ(mapper.get_full_name(gid), path);
        }
    }
}

TEST(DSNameMapperDispatchTest, SameNameSiblingsResolveToFirstNode) {
    // 同名兄弟（非法树形态的防御场景——⑮ 实例名在 block 内唯一）：
    // 索引构建按节点 id 升序、已存在跳过 → 指向首个，与原
    // find_child_by_instance_name 的 children 首个命中一致
    DispatchEnv env;
    // 手工追加同名兄弟：node 52 与 node 2（leaf_0）同实例名
    DSHierNode dup;
    dup.id_ = 52;
    dup.parent_id_ = 1;
    dup.block_cell_name_ = "leafcell";
    dup.instance_name_ = "leaf_0";  // 与 node 2 同名
    dup.block_cell_id_ = DispatchEnv::kLeafCellId;
    dup.self_global_id_ = 100;
    dup.instance_start_ = 1000;
    dup.instance_count_ = 1 + DispatchEnv::kLeafLocalCount;
    env.tree.nodes_.push_back(std::move(dup));
    env.tree.nodes_[1].get_ref_children_ids().push_back(52);

    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    mapper.set_block_hasher(DispatchEnv::kLeafCellId,
                            env.leaf_names->instance_names_);
    // 分派命中首个（node 2，区间起点 2 的后继）而非后登记者（node 52）
    EXPECT_EQ(mapper.get_global_id("top/m1/leaf_0/f0"),
              env.expect_id(0, 1));
}

TEST(DSNameMapperDispatchTest, IllegalPathsStayInvalid) {
    DispatchEnv env;
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    mapper.set_block_hasher(DispatchEnv::kLeafCellId,
                            env.leaf_names->instance_names_);

    // 首段不匹配（索引键皆以 root 实例名开头）
    EXPECT_EQ(mapper.get_global_id("ghost/m1/leaf_0/f0"),
              DSInstanceNameMapper::kInvalidId);
    // 空中间段（连续分隔符）
    EXPECT_EQ(mapper.get_global_id("top//leaf_0/f0"),
              DSInstanceNameMapper::kInvalidId);
    // 首段为空（前导分隔符）
    EXPECT_EQ(mapper.get_global_id("/top/m1/leaf_0/f0"),
              DSInstanceNameMapper::kInvalidId);
    // 叶段为空（尾随分隔符）
    EXPECT_EQ(mapper.get_global_id("top/m1/leaf_0/"),
              DSInstanceNameMapper::kInvalidId);
    // 无分隔单段
    EXPECT_EQ(mapper.get_global_id("top"),
              DSInstanceNameMapper::kInvalidId);
    // 多余尾段（叶段之后还有段——原实现中间段断裂语义，保持未命中）
    EXPECT_EQ(mapper.get_global_id("top/m1/leaf_0/f0/extra"),
              DSInstanceNameMapper::kInvalidId);
    // 中间段断裂
    EXPECT_EQ(mapper.get_global_id("top/m9/leaf_0/f0"),
              DSInstanceNameMapper::kInvalidId);
    // 叶名未登记
    EXPECT_EQ(mapper.get_global_id("top/m1/leaf_0/f9"),
              DSInstanceNameMapper::kInvalidId);
    // 空路径
    EXPECT_EQ(mapper.get_global_id(""), DSInstanceNameMapper::kInvalidId);
}

TEST(DSNameMapperDispatchTest, SetTreeRebuildsDispatchIndex) {
    DispatchEnv env;
    MapperEnv other_env;  // 另一棵树（top/i2/...，见文件头 MapperEnv）
    DSInstanceNameMapper mapper;
    // 默认构造（无树）→ 查询未命中
    EXPECT_EQ(mapper.get_global_id("top/m1/leaf_0/f0"),
              DSInstanceNameMapper::kInvalidId);
    // set_tree 挂第一棵树 → 分派索引自动生效
    mapper.set_tree(&env.tree);
    mapper.set_block_hasher(DispatchEnv::kLeafCellId,
                            env.leaf_names->instance_names_);
    EXPECT_EQ(mapper.get_global_id("top/m1/leaf_0/f0"),
              env.expect_id(0, 1));
    // 换树 → 旧键失效、新树键生效（自动重建，无忘重建静默错）
    attach_cell_ids(other_env);
    mapper.set_tree(&other_env.tree);
    mapper.set_block_hasher(2, other_env.bottom.names->instance_names_);
    EXPECT_EQ(mapper.get_global_id("top/m1/leaf_0/f0"),
              DSInstanceNameMapper::kInvalidId);
    EXPECT_EQ(mapper.get_global_id("top/i2/i1/i1"), 8u);
    // 显式重建入口（树原地修改后的兜底）：空树重建后一切未命中
    mapper.set_kind(DSNameMapperKind::INSTANCE);
    other_env.tree.nodes_.clear();
    mapper.rebuild_dispatch_index();
    EXPECT_EQ(mapper.get_global_id("top/i2/i1/i1"),
              DSInstanceNameMapper::kInvalidId);
}

TEST(DSNameMapperDispatchTest, IndexBuildCostObservable) {
    // 索引构建开销观测（非断言——基准报告引用；万级节点毫秒量级）
    DispatchEnv env;
    const auto t0 = std::chrono::steady_clock::now();
    DSInstanceNameMapper mapper(&env.tree, DSNameMapperKind::INSTANCE);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                .count()) /
        1e6;
    std::printf("R8C dispatch index build: 52 nodes in %.3f ms\n", ms);
    EXPECT_EQ(mapper.injected_count(), 0u);
}

}  // namespace
