// MonitorSampler / SystemInfo jiffies / NetStats 单测：
// 纯函数边界 + 真实 /proc 采样烟囱 + jiffy 解析自洽性。
#include <gtest/gtest.h>
#include <monitor/cpp/monitor_sampler.h>
#include <core/cpp/system_info.h>
#include <network/cpp/net_stats.h>

#include <thread>

namespace {

TEST(MonitorSamplerPureTest, RatioBpsBoundaries) {
    // 分母非正 / 分子为负 → 0。
    EXPECT_EQ(fly::MonitorSampler::ratio_bps(50, 0), 0u);
    EXPECT_EQ(fly::MonitorSampler::ratio_bps(50, -1), 0u);
    EXPECT_EQ(fly::MonitorSampler::ratio_bps(-5, 100), 0u);
    // 常规换算：25/100 = 2500bps（25%）。
    EXPECT_EQ(fly::MonitorSampler::ratio_bps(25, 100), 2500u);
    // 满载与钳制。
    EXPECT_EQ(fly::MonitorSampler::ratio_bps(100, 100), 10000u);
    EXPECT_EQ(fly::MonitorSampler::ratio_bps(150, 100), 10000u);
    // 向下取整：1/3 = 33.33% = 3333bps。
    EXPECT_EQ(fly::MonitorSampler::ratio_bps(1, 3), 3333u);
}

TEST(MonitorSamplerPureTest, HostCpuBpsBoundaries) {
    // 全 idle → 0；全忙 → 10000。
    EXPECT_EQ(fly::MonitorSampler::host_cpu_bps(1000, 1000), 0u);
    EXPECT_EQ(fly::MonitorSampler::host_cpu_bps(1000, 0), 10000u);
    // d_idle > d_total（计数器错位）钳 0。
    EXPECT_EQ(fly::MonitorSampler::host_cpu_bps(1000, 1200), 0u);
    EXPECT_EQ(fly::MonitorSampler::host_cpu_bps(0, 0), 0u);
    // 70% 忙。
    EXPECT_EQ(fly::MonitorSampler::host_cpu_bps(1000, 300), 7000u);
}

TEST(SystemInfoJiffiesTest, ProcessAndHostReadable) {
    const int64_t j1 = fly::SystemInfo::process_cpu_jiffies();
    EXPECT_GE(j1, 0) << "/proc/self/stat utime+stime 应可读";
    const fly::SystemInfo::HostCpu hc = fly::SystemInfo::host_cpu_jiffies();
    EXPECT_GT(hc.total_, 0) << "/proc/stat cpu 汇总行应可读";
    EXPECT_GE(hc.idle_, 0);
    EXPECT_LE(hc.idle_, hc.total_);
}

// 烧 CPU 直到进程 jiffies 与 host total jiffies 都确定前进 ≥1。
// 两口径机制不同：进程 utime/stime 精确记账（<1 tick 窗口即可前进），host
// /proc/stat 按 tick 粒度更新且 WSL2 所有 vCPU tick 同相（total 最小步长=核数）
// ——短采样窗口 d_total 可整 0（100 轮压测实测撞过），断言 proc_cpu_bps_>0
// 的前提必须双差分非零。guard 防 /proc 读取异常时死循环。
namespace {
struct CpuJiffies { int64_t proc = -1, host_total = -1; };

CpuJiffies read_cpu_jiffies() {
    CpuJiffies j;
    j.proc = fly::SystemInfo::process_cpu_jiffies();
    j.host_total = fly::SystemInfo::host_cpu_jiffies().total_;
    return j;
}

CpuJiffies burn_until_both_advance(CpuJiffies from) {
    volatile uint64_t sink = 0;
    CpuJiffies now = from;
    for (int guard = 0; guard < 200; ++guard) {
        if (now.proc > from.proc && now.host_total > from.host_total) break;
        for (int i = 0; i < 10000000; ++i) sink += i;
        now = read_cpu_jiffies();
    }
    return now;
}
}  // namespace

TEST(SystemInfoJiffiesTest, ProcessJiffiesMonotonicUnderBusyLoop) {
    const CpuJiffies j1 = read_cpu_jiffies();
    ASSERT_GE(j1.proc, 0) << "/proc/self/stat 应可读";
    ASSERT_GE(j1.host_total, 0) << "/proc/stat 应可读";
    const CpuJiffies j2 = burn_until_both_advance(j1);
    EXPECT_GT(j2.proc, j1.proc) << "忙循环应至少消耗 1 jiffy CPU 时间";
}

TEST(MonitorSamplerTest, SampleOnceSmoke) {
    fly::MonitorSampler sampler;
    // 首采无基线：CPU 记 0，其余维度有效。
    fly::MonitorSample first = sampler.sample_once();
    EXPECT_GT(first.epoch_ms_, 0u);
    EXPECT_GT(first.proc_rss_bytes_, 0u);
    EXPECT_GT(first.host_mem_total_bytes_, 0u);
    EXPECT_LE(first.host_mem_avail_bytes_, first.host_mem_total_bytes_);
    EXPECT_EQ(first.proc_cpu_bps_, 0u);
    EXPECT_EQ(first.host_cpu_bps_, 0u);

    // 烧 CPU 到进程与 host 的 jiffies 记账都确定前进 ≥1 tick 再采：保证差分
    // 非零（短窗口 d_total 可整 0——WSL2 tick 同相 + 精确/采样两种记账口径）。
    const CpuJiffies j1 = read_cpu_jiffies();
    ASSERT_GE(j1.proc, 0) << "/proc/self/stat 应可读";
    const CpuJiffies j2 = burn_until_both_advance(j1);
    ASSERT_GT(j2.proc, j1.proc) << "忙循环应至少消耗 1 jiffy CPU 时间";
    ASSERT_GT(j2.host_total, j1.host_total) << "host total jiffies 应至少前进 1 tick";
    fly::MonitorSample second = sampler.sample_once();
    EXPECT_GT(second.epoch_ms_, first.epoch_ms_);
    EXPECT_LE(second.proc_cpu_bps_, 10000u);
    EXPECT_LE(second.host_cpu_bps_, 10000u);
    EXPECT_GT(second.proc_cpu_bps_, 0u) << "忙循环后进程 CPU% 应非零";
    EXPECT_GT(second.host_mem_total_bytes_, 0u);
}

TEST(NetStatsTest, CountersAccumulate) {
    fly::NetStats& stats = fly::NetStats::instance();
    const uint64_t r0 = stats.read_bytes();
    const uint64_t w0 = stats.write_bytes();
    stats.add_read(100);
    stats.add_read(23);
    stats.add_write(77);
    EXPECT_EQ(stats.read_bytes() - r0, 123u);
    EXPECT_EQ(stats.write_bytes() - w0, 77u);
}

// 事件驱动采样前提：多线程并发 sample_once 必须安全且 epoch 单调
//（差分状态互斥保护；混序会导致差分基线错乱）。
TEST(MonitorSamplerTest, ConcurrentSampleOnceIsSafeAndMonotonic) {
    fly::MonitorSampler sampler;
    (void)sampler.sample_once();  // 建立差分基线
    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;
    std::vector<std::vector<uint64_t>> epochs(kThreads);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&sampler, &epochs, t] {
            for (int i = 0; i < kPerThread; ++i) {
                epochs[t].push_back(sampler.sample_once().epoch_ms_);
            }
        });
    }
    for (auto& th : threads) th.join();
    // 各线程自身 epoch 单调不减。
    for (const auto& e : epochs) {
        for (size_t i = 1; i < e.size(); ++i) {
            EXPECT_GE(e[i], e[i - 1]);
        }
    }
}

// kind 字段：周期(0)/事件(1)标记随样本结构传递。
TEST(MonitorSamplerTest, SampleKindFieldDefaultZero) {
    fly::MonitorSampler sampler;
    fly::MonitorSample sp = sampler.sample_once();
    EXPECT_EQ(sp.kind_, 0u);  // 采样器产周期样本；事件标记由调用方置 1
}

}  // namespace
