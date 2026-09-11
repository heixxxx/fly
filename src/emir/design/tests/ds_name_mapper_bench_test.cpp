// DSNameMapperT 分派查询路径性能基准（R8c，裁定 53 补充测试要求；方案
// docs/emir/design-db-phase2-plan.md 53 行）：
//
//   完整 namemapper 查询路径前后对比基准——get_global_id 正向全路径
//   （拆路径 → 树逐层分派 → 叶层 hasher → 区间换算）与 get_full_name
//   反向全路径（区间反查 → 叶层 hasher → 向上拼路径）。分派优化
//   （方案 B：block 层次全路径前缀索引）实施前后各跑同一矩阵，量化
//   分派优化收益（报告 .work/bench_name_mapper/DISPATCH_BENCH.md）。
//
//   场景矩阵 = 层级深度（2/4/8 层）× 叶层扇出（直接 children
//   10/100/1000）× 叶层 hasher 名数（10 万/100 万）= 18 格，每格测
//   正向命中 / 正向未命中 / 反向命中三口径。
//
//   树形态 = 主链 + 叶层扇出：root 实例名 "top"，主链节点 "m1".."m{d-2}"
//   （每层一个直接 child），叶层 fanout 个实例 "leaf_<k>"——同一 block
//   cell 定义（cell id 7）多实例共享同一份叶 hasher（与 ds_name_mapper_test
//   的 bottom ×2 共享语义同构）；区间按 DFS 前序连续分配（父区间覆盖
//   自身 + 子树，与 ds_build_hier_tree 产物同构）。
//
//   指标口径：p50/p99 = 逐次 steady_clock 计时的分位（含计时开销，
//   前后同口径公平对比）；QPS = 吞吐轮（连续调用不逐次计时）的总吞吐，
//   不含计时开销。正确性先于计时：每格先抽样断言正向期望 id、双向闭环、
//   未命中哨兵，校验失败即 FAIL（基准数据无效）。
//
// 默认 DISABLED_（基准非正确性测试，不进常规全量测试集），手动运行：
//   bazel-bin/src/emir/design/tests/ds_name_mapper_bench_test \
//     --gtest_filter='DSNameMapperBench.*'
// 输出行（前后对比按 kind/dir/depth/fanout/names 对齐）：
//   R8C kind=hit|miss dir=fwd|rev depth=D fanout=F names=N q=Q
//       p50_us=.. p99_us=.. qps=.. hwm_kb=..
#include <emir/design/cpp/ds_name_hasher.h>
#include <emir/design/cpp/ds_name_mapper.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <sys/resource.h>

namespace {

using namespace fly;

// 每格命中/未命中/反向的查询条数与抽样校验条数（10 万分位样本足够
// 稳定；跑时控制在分钟级内）
constexpr int kQueryCount = 100000;
constexpr int kMissCount = 10000;
constexpr int kVerifyCount = 2000;

// 叶层 block cell id（全部叶节点同一定义——共享同一份叶 hasher）
constexpr uint32_t kLeafCellId = 7;

// 查询结果累计（消费返回值防编译器消除；测试体末打印便于人工核对
// 前后两轮一致性）
uint64_t g_sink = 0;

// ── 层级树装置（主链 + 叶层扇出；区间 DFS 前序连续分配）──────────────

struct BenchTree {
    DSHierTree tree;
    // 叶层各节点的 instance 区间起点（local 0 占位 + names 个名）
    std::vector<uint64_t> leaf_start;
};

// 递归分配区间：自身 local 0 占位 → 依次子树；count = 自身 + 子树总和；
// 子节点 self_global_id = 父区间内第 (child 序 + 1) 个 local。
// 叶层节点额外承载叶 hasher 的 local 空间（names_count 个名，local
// 1..names_count——叶名 id 落在 [start+1, start+names_count]）
uint64_t assign_ranges(DSHierTree& tree, uint32_t node_id, uint64_t cursor,
                       uint64_t names_count,
                       std::vector<uint64_t>& leaf_start) {
    DSHierNode& n = tree.nodes_[node_id];
    n.instance_start_ = cursor;
    cursor += 1;  // 自身占位（⑧ local 0）
    if (n.block_cell_id_ == kLeafCellId) {
        cursor += names_count;  // 叶 hasher local 空间（local 1..N）
    }
    size_t child_index = 0;
    for (const uint32_t child : n.children_ids_) {
        tree.nodes_[child].self_global_id_ =
            n.instance_start_ + child_index + 1;
        cursor = assign_ranges(tree, child, cursor, names_count, leaf_start);
        ++child_index;
    }
    n.instance_count_ = cursor - n.instance_start_;
    if (n.block_cell_id_ == kLeafCellId) {
        leaf_start.push_back(n.instance_start_);
    }
    return cursor;
}

BenchTree make_bench_tree(int depth, int fanout, uint64_t names_count) {
    BenchTree bt;
    DSHierTree& tree = bt.tree;
    bt.leaf_start.reserve(static_cast<size_t>(fanout));
    // 节点 id = DFS 前序 = 下标序：root(0)、主链 m1..m_{d-2}、叶层 ×F
    tree.nodes_.resize(static_cast<size_t>(depth - 1) +
                       static_cast<size_t>(fanout));
    for (size_t i = 0; i < tree.nodes_.size(); ++i) {
        DSHierNode& n = tree.nodes_[i];
        n.id_ = static_cast<uint32_t>(i);
        n.parent_id_ = static_cast<uint32_t>(i);
    }
    DSHierNode& root = tree.nodes_[0];
    root.block_cell_name_ = "top";
    root.instance_name_ = "top";
    tree.design_name_ = "top";
    // 主链（每层一个直接 child）
    for (int d = 1; d + 1 < depth; ++d) {
        DSHierNode& m = tree.nodes_[static_cast<size_t>(d)];
        m.parent_id_ = static_cast<uint32_t>(d - 1);
        m.block_cell_name_ = "mid";
        m.instance_name_ = "m" + std::to_string(d);
        tree.nodes_[static_cast<size_t>(d - 1)]
            .get_ref_children_ids()
            .push_back(static_cast<uint32_t>(d));
    }
    // 叶层：父 = 主链末端（depth=2 时父 = root）
    const uint32_t leaf_parent =
        depth >= 3 ? static_cast<uint32_t>(depth - 2) : 0u;
    for (int k = 0; k < fanout; ++k) {
        DSHierNode& leaf = tree.nodes_[static_cast<size_t>(depth - 1 + k)];
        leaf.parent_id_ = leaf_parent;
        leaf.block_cell_name_ = "leafcell";
        leaf.instance_name_ = "leaf_" + std::to_string(k);
        leaf.block_cell_id_ = kLeafCellId;
        tree.nodes_[leaf_parent].get_ref_children_ids().push_back(leaf.id_);
    }
    assign_ranges(tree, 0, 0, names_count, bt.leaf_start);
    return bt;
}

// ── 叶 hasher 名集（仿真系统名形态 n%07d，local id 从 1 起）──────────

CMSharedPtr<DSInstanceNameHasher> make_leaf_hasher(uint64_t names_count) {
    auto hasher = CMMakeShared<DSInstanceNameHasher>();
    char buf[32];
    for (uint64_t i = 1; i <= names_count; ++i) {
        std::snprintf(buf, sizeof(buf), "n%07llu",
                      static_cast<unsigned long long>(i));
        hasher->assign(buf, i);
    }
    return hasher;
}

// ── 计时与统计 ────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;

int64_t elapsed_ns(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

// 分位（ns）：n 元样本的 p50 / p99（会部分重排样本，统计后即弃）
void percentiles(std::vector<uint64_t>& lat, double& p50_us, double& p99_us) {
    const size_t n = lat.size();
    std::nth_element(lat.begin(), lat.begin() + static_cast<long>(n / 2),
                     lat.end());
    p50_us = static_cast<double>(lat[n / 2]) / 1000.0;
    std::nth_element(lat.begin(),
                     lat.begin() + static_cast<long>(n * 99 / 100),
                     lat.end());
    p99_us = static_cast<double>(lat[n * 99 / 100]) / 1000.0;
}

long current_hwm_kb() {
    rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;  // Linux = KB
}

void emit_row(const char* kind, const char* dir, int depth, int fanout,
              uint64_t names, int q, double p50_us, double p99_us,
              double qps) {
    std::printf(
        "R8C kind=%s dir=%s depth=%d fanout=%d names=%llu q=%d "
        "p50_us=%.3f p99_us=%.3f qps=%.0f hwm_kb=%ld\n",
        kind, dir, depth, fanout, static_cast<unsigned long long>(names), q,
        p50_us, p99_us, qps, current_hwm_kb());
    std::fflush(stdout);
}

// ── 单格基准（树 + mapper + 样本 + 校验 + 正向/反向计时）──────────────

void run_grid(int depth, int fanout, uint64_t names_count,
              const CMSharedPtr<DSInstanceNameHasher>& leaf_hasher) {
    // 装置：树 + mapper（叶 cell 注入共享 hasher）
    BenchTree bt = make_bench_tree(depth, fanout, names_count);
    DSInstanceNameMapper mapper(&bt.tree, DSNameMapperKind::INSTANCE);
    mapper.set_block_hasher(kLeafCellId, leaf_hasher);

    // 查询样本：随机叶节点 × 随机叶名（固定 seed 可复现）
    // 路径 = "top/m1/.../leaf_<k>/n%07d"；期望 id = 叶区间起点 + local
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> leaf_pick(0, fanout - 1);
    std::uniform_int_distribution<uint64_t> name_pick(0, names_count - 1);
    std::string base = "top";
    for (int d = 1; d + 1 < depth; ++d) {
        base += "/m" + std::to_string(d);
    }
    char leaf_name[32];
    std::vector<CMString> paths(static_cast<size_t>(kQueryCount));
    std::vector<uint64_t> expect(static_cast<size_t>(kQueryCount));
    for (int q = 0; q < kQueryCount; ++q) {
        const int k = leaf_pick(rng);
        const uint64_t ni = name_pick(rng);
        std::snprintf(leaf_name, sizeof(leaf_name), "n%07llu",
                      static_cast<unsigned long long>(ni + 1));
        paths[static_cast<size_t>(q)] =
            base + "/leaf_" + std::to_string(k) + "/" + leaf_name;
        expect[static_cast<size_t>(q)] =
            bt.leaf_start[static_cast<size_t>(k)] + ni + 1;
    }

    // 正确性校验（先于计时——数据无效的基准不如没有数据）：
    // 抽样正向期望 id + 双向闭环
    for (int q = 0; q < kVerifyCount; ++q) {
        ASSERT_EQ(mapper.get_global_id(paths[static_cast<size_t>(q)]),
                  expect[static_cast<size_t>(q)])
            << "fwd hit mismatch at q=" << q;
        ASSERT_EQ(mapper.get_full_name(expect[static_cast<size_t>(q)]),
                  paths[static_cast<size_t>(q)])
            << "round-trip mismatch at q=" << q;
    }

    // 正向命中：逐次计时轮（p50/p99）
    std::vector<uint64_t> lat(static_cast<size_t>(kQueryCount));
    for (int q = 0; q < kQueryCount; ++q) {
        const auto a = Clock::now();
        const uint64_t id =
            mapper.get_global_id(paths[static_cast<size_t>(q)]);
        const auto b = Clock::now();
        lat[static_cast<size_t>(q)] = static_cast<uint64_t>(elapsed_ns(a, b));
        g_sink += id;
    }
    double p50 = 0.0;
    double p99 = 0.0;
    percentiles(lat, p50, p99);
    // 正向命中：吞吐轮（连续调用不逐次计时）
    const auto t0 = Clock::now();
    for (int q = 0; q < kQueryCount; ++q) {
        g_sink += mapper.get_global_id(paths[static_cast<size_t>(q)]);
    }
    const auto t1 = Clock::now();
    const double hit_secs = static_cast<double>(elapsed_ns(t0, t1)) / 1e9;
    emit_row("hit", "fwd", depth, fanout, names_count, kQueryCount, p50, p99,
             static_cast<double>(kQueryCount) / hit_secs);

    // 正向未命中（叶名不存在 → 叶层 hasher 未命中）：期望全量哨兵
    std::vector<CMString> miss_paths(static_cast<size_t>(kMissCount));
    for (int q = 0; q < kMissCount; ++q) {
        const int k = leaf_pick(rng);
        std::snprintf(leaf_name, sizeof(leaf_name), "g%07llu",
                      static_cast<unsigned long long>(name_pick(rng)));
        miss_paths[static_cast<size_t>(q)] =
            base + "/leaf_" + std::to_string(k) + "/" + leaf_name;
    }
    for (int q = 0; q < kMissCount; ++q) {
        ASSERT_EQ(mapper.get_global_id(miss_paths[static_cast<size_t>(q)]),
                  DSInstanceNameMapper::kInvalidId)
            << "miss sentinel mismatch at q=" << q;
    }
    for (int q = 0; q < kMissCount; ++q) {
        const auto a = Clock::now();
        const uint64_t id =
            mapper.get_global_id(miss_paths[static_cast<size_t>(q)]);
        const auto b = Clock::now();
        lat[static_cast<size_t>(q)] = static_cast<uint64_t>(elapsed_ns(a, b));
        g_sink += id;
    }
    percentiles(lat, p50, p99);
    const auto m0 = Clock::now();
    for (int q = 0; q < kMissCount; ++q) {
        g_sink += mapper.get_global_id(miss_paths[static_cast<size_t>(q)]);
    }
    const auto m1 = Clock::now();
    const double miss_secs = static_cast<double>(elapsed_ns(m0, m1)) / 1e9;
    emit_row("miss", "fwd", depth, fanout, names_count, kMissCount, p50, p99,
             static_cast<double>(kMissCount) / miss_secs);

    // 反向命中（区间反查 + 叶 hasher get_name + 拼路径；分派优化不触碰
    // 该路径——同矩阵复测确认无回归）：闭环已在校验轮断言，直接计时
    for (int q = 0; q < kQueryCount; ++q) {
        const auto a = Clock::now();
        const CMString name =
            mapper.get_full_name(expect[static_cast<size_t>(q)]);
        const auto b = Clock::now();
        lat[static_cast<size_t>(q)] = static_cast<uint64_t>(elapsed_ns(a, b));
        g_sink += name.size();
    }
    percentiles(lat, p50, p99);
    const auto r0 = Clock::now();
    for (int q = 0; q < kQueryCount; ++q) {
        g_sink +=
            mapper.get_full_name(expect[static_cast<size_t>(q)]).size();
    }
    const auto r1 = Clock::now();
    const double rev_secs = static_cast<double>(elapsed_ns(r0, r1)) / 1e9;
    emit_row("hit", "rev", depth, fanout, names_count, kQueryCount, p50, p99,
             static_cast<double>(kQueryCount) / rev_secs);
}

TEST(DSNameMapperBench, DISABLED_DispatchMatrix) {
    // 名集两档（叶 hasher 跨格复用——只读查询，档内零重建）
    for (const uint64_t names_count :
         {uint64_t{100000}, uint64_t{1000000}}) {
        const CMSharedPtr<DSInstanceNameHasher> leaf_hasher =
            make_leaf_hasher(names_count);
        for (const int depth : {2, 4, 8}) {
            for (const int fanout : {10, 100, 1000}) {
                run_grid(depth, fanout, names_count, leaf_hasher);
            }
        }
    }
    std::printf("R8C done sink=%llu\n",
                static_cast<unsigned long long>(g_sink));
}

}  // namespace
