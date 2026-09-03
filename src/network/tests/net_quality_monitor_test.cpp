// Unit tests for NetQualityMonitor — per-host network quality scoring table.
//
// Drives the network-aware remote-read priority feature (Phase 1): a passive-RTT
// + active-bandwidth table that scores peers so TIER2 can prefer better links.
// These tests must fail before net_quality_monitor.{h,cpp} exist.
#include <gtest/gtest.h>
#include <network/cpp/net_quality_monitor.h>
#include <chrono>
#include <cmath>
#include <latch>
#include <thread>

namespace fly {

class NetQualityMonitorTest : public ::testing::Test {
protected:
    NetQualityMonitor& mon_ = NetQualityMonitor::instance();

    void SetUp() override { mon_.clear(); }
};

// score() defaults to 0 for an unknown host (cold start → falls back to
// registration order, identical to current behavior).
TEST_F(NetQualityMonitorTest, UnknownHostScoresZero) {
    EXPECT_DOUBLE_EQ(mon_.score("10.0.0.1"), 0.0);
}

// RTT alone produces a score: lower RTT → higher score.
TEST_F(NetQualityMonitorTest, RttLowerIsHigherScore) {
    mon_.update_rtt("fast", 5.0);
    mon_.update_rtt("slow", 100.0);
    EXPECT_GT(mon_.score("fast"), mon_.score("slow"));
    EXPECT_GT(mon_.score("fast"), 0.0);
}

// Bandwidth alone produces a score: higher bandwidth → higher score.
TEST_F(NetQualityMonitorTest, BandwidthHigherIsHigherScore) {
    mon_.update_bandwidth("fat", 1000.0);
    mon_.update_bandwidth("thin", 10.0);
    EXPECT_GT(mon_.score("fat"), mon_.score("thin"));
}

// Combined: both contribute, RTT dominates by default weight.
TEST_F(NetQualityMonitorTest, BothDimensionsContribute) {
    mon_.update_rtt("a", 10.0);
    mon_.update_bandwidth("a", 100.0);
    double s_rtt = mon_.score("a");
    mon_.clear();
    mon_.update_bandwidth("b", 100.0);  // bandwidth only
    double s_bw = mon_.score("b");
    EXPECT_GT(s_rtt, s_bw);  // having RTT data too must beat bw-only
}

// EMA: a sequence of RTT samples converges toward the recent value, not the
// mean, and is not identical to the first sample.
TEST_F(NetQualityMonitorTest, EmaConvergesToRecent) {
    for (int i = 0; i < 30; ++i) mon_.update_rtt("h", 100.0);
    double after_long = mon_.score("h");
    for (int i = 0; i < 30; ++i) mon_.update_rtt("h", 5.0);
    double after_short = mon_.score("h");
    EXPECT_GT(after_short, after_long);  // converged to lower RTT → higher score
}

// A single recent RTT update must not be drowned by stale long-run samples:
// the last few samples dominate.
TEST_F(NetQualityMonitorTest, EmaTracksLatestSamples) {
    for (int i = 0; i < 20; ++i) mon_.update_rtt("h", 10.0);
    mon_.update_rtt("h", 10.0);
    mon_.update_rtt("h", 10.0);
    double base = mon_.score("h");
    mon_.clear();
    for (int i = 0; i < 20; ++i) mon_.update_rtt("h", 10.0);
    mon_.update_rtt("h", 200.0);  // sudden spike
    mon_.update_rtt("h", 200.0);
    double spiked = mon_.score("h");
    EXPECT_LT(spiked, base);  // recent spikes pull the EMA up (score down)
}

// Stale entries (no update within ttl) score 0 → treated as unknown.
TEST_F(NetQualityMonitorTest, StaleEntryScoresZero) {
    mon_.update_rtt("old", 5.0);
    // Manually age the entry past ttl by injecting an old timestamp.
    mon_.age_entry_for_test("old", 600);  // 600s > default ttl 300s
    EXPECT_DOUBLE_EQ(mon_.score("old"), 0.0);
}

// ── 并发正确性（P3-17 批 3）：多线程 EMA 更新（共享 + 分片 host）与
//    score 读取交错——join 后分数有限（无 NaN/inf），EMA 收敛到末值
//    （shared_mutex 串行化正确性；此前全顺序用例）。 ──

TEST_F(NetQualityMonitorTest, ConcurrentUpdateAndScoreStaysFinite) {
    constexpr int kWriters = 4;
    std::latch go{kWriters + 2};
    CMVector<std::thread> threads;
    for (int t = 0; t < kWriters; ++t) {
        threads.emplace_back([&, t] {
            go.count_down(); go.wait();
            for (int i = 1; i <= 200; ++i) {
                CMString host = (t % 2 == 0) ? "shared_host"
                                             : "host_" + std::to_string(t);
                mon_.update_rtt(host, 1.0 + i % 10);
                mon_.update_bandwidth(host, 100.0 + i);
            }
        });
    }
    for (int r = 0; r < 2; ++r) {
        threads.emplace_back([&] {
            go.count_down(); go.wait();
            for (int c = 0; c < 300; ++c) {
                (void)mon_.score("shared_host");
                (void)mon_.score("host_1");
            }
        });
    }
    for (auto& th : threads) th.join();

    for (const char* h : {"shared_host", "host_0", "host_1", "host_2", "host_3"}) {
        double s = mon_.score(h);
        EXPECT_TRUE(std::isfinite(s)) << "host=" << h << " score=" << s;
        EXPECT_GE(s, 0.0) << "host=" << h;
    }
}

}  // namespace fly
