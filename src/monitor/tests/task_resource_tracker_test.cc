// TaskResourceTracker 单测：窗口归属、短 task ≥2 样本、结算取出即清、
// 错配窗口防御、并发 add_sample。
// 注意：begin/end 的立即采样取真实进程 RSS（非测试可控），测试先以一个
// 预热窗口读回 baseline 再做精确断言；喂入的窗口内样本用远大于真实 RSS
// 的值确保成为 peak。
#include <gtest/gtest.h>
#include <monitor/cpp/task_resource_tracker.h>

#include <thread>
#include <vector>

namespace {

// 远大于测试进程真实 RSS 的假样本值（10GB 级），确保可预测地成为 peak。
constexpr uint64_t kBigA = 20ull << 30;
constexpr uint64_t kBigB = 10ull << 30;

TEST(TaskResourceTrackerTest, BasicWindowAggregation) {
    fly::TaskResourceTracker tr;

    tr.begin(5);
    tr.add_sample(kBigB + 50);
    tr.add_sample(kBigB + 200);  // peak
    tr.add_sample(kBigB + 30);
    tr.end(5);

    fly::TaskResourceAgg agg;
    ASSERT_TRUE(tr.take_agg(5, agg));
    const uint64_t b = agg.mem_baseline_bytes_;  // 真实进程 RSS
    EXPECT_GT(b, 0u);
    EXPECT_EQ(agg.mem_peak_bytes_, kBigB + 200);
    EXPECT_EQ(agg.sample_count_, 5u);  // begin + 3 add + end 各一
    // 全部样本 ∈ [min(b, kBigB), kBigB+200]，avg 必在开区间内（验证参与度）。
    EXPECT_GT(agg.mem_avg_bytes_, kBigB / 2);
    EXPECT_LE(agg.mem_avg_bytes_, kBigB + 200);
    EXPECT_GT(agg.exec_end_ms_, 0u);
    EXPECT_GE(agg.exec_end_ms_, agg.exec_start_ms_);  // 不倒退
    EXPECT_GE(agg.cpu_time_ms_, 0u);

    // 取出即清：重复 take 返回 false（重复发送不重复填）。
    fly::TaskResourceAgg again;
    EXPECT_FALSE(tr.take_agg(5, again));
}

TEST(TaskResourceTrackerTest, ShortTaskHasAtLeastTwoSamples) {
    fly::TaskResourceTracker tr;
    tr.begin(7);
    tr.end(7);  // 无任何 add_sample：begin/end 各一次立即采样
    fly::TaskResourceAgg agg;
    ASSERT_TRUE(tr.take_agg(7, agg));
    EXPECT_GT(agg.mem_baseline_bytes_, 0u);  // 真实进程 RSS 非零
    EXPECT_EQ(agg.sample_count_, 2u);        // begin/end 各一
    EXPECT_GE(agg.mem_peak_bytes_, agg.mem_baseline_bytes_);
    EXPECT_GE(agg.mem_avg_bytes_, agg.mem_baseline_bytes_ / 2);
}

TEST(TaskResourceTrackerTest, MismatchedEndDropsWindow) {
    fly::TaskResourceTracker tr;
    tr.begin(1);
    tr.add_sample(kBigA);
    tr.end(999);  // 错配：窗口丢弃，不留结算
    fly::TaskResourceAgg agg;
    EXPECT_FALSE(tr.take_agg(1, agg));
    EXPECT_FALSE(tr.take_agg(999, agg));

    // 丢弃后新窗口干净开始：kBigA（旧窗口样本）未混入，peak 是新窗口的 kBigB。
    tr.begin(2);
    tr.add_sample(kBigB);
    tr.end(2);
    ASSERT_TRUE(tr.take_agg(2, agg));
    EXPECT_EQ(agg.mem_peak_bytes_, kBigB);
}

TEST(TaskResourceTrackerTest, AddSampleWithoutWindowIgnored) {
    fly::TaskResourceTracker tr;
    tr.add_sample(kBigA);  // 无窗口在跑：忽略（若混入会污染后续窗口 peak）
    tr.begin(3);
    tr.add_sample(kBigB);
    tr.end(3);
    fly::TaskResourceAgg agg;
    ASSERT_TRUE(tr.take_agg(3, agg));
    EXPECT_EQ(agg.mem_peak_bytes_, kBigB);  // kBigA > kBigB：peak 不是它即证明未混入
}

TEST(TaskResourceTrackerTest, ConcurrentAddSampleWhileBeginEnd) {
    fly::TaskResourceTracker tr;
    constexpr int kThreads = 4;
    constexpr int kSamples = 10000;
    constexpr uint64_t V = 1000;  // < 真实 RSS，peak 由 begin/end 的真实采样决定
    std::vector<std::thread> adders;
    for (int t = 0; t < kThreads; ++t) {
        adders.emplace_back([&tr] {
            for (int i = 0; i < kSamples; ++i) {
                tr.add_sample(V);
            }
        });
    }
    tr.begin(42);
    for (auto& th : adders) th.join();
    tr.end(42);
    fly::TaskResourceAgg agg;
    ASSERT_TRUE(tr.take_agg(42, agg));
    // 计数守恒（并发 add 无丢失）：begin 1 + 4*10000 + end 1。
    EXPECT_EQ(agg.sample_count_, 1u + kThreads * kSamples + 1u);
    // avg = (b + 40000·V + b')/40002，b/b' 为真实 RSS（MB 级）：
    // V < avg < V + 4000（丢样本会显著拉高 avg，RSS 项贡献 < 2·20MB/40002）。
    EXPECT_GT(agg.mem_avg_bytes_, V);
    EXPECT_LT(agg.mem_avg_bytes_, V + 4000);
    // peak 是真实 RSS（begin/end 采样），V 未顶上去。
    EXPECT_GT(agg.mem_peak_bytes_, V);
    EXPECT_LT(agg.mem_peak_bytes_, 1ull << 30);
}

}  // namespace
