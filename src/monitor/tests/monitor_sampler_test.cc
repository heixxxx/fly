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

TEST(SystemInfoJiffiesTest, ProcessJiffiesMonotonicUnderBusyLoop) {
    const int64_t j1 = fly::SystemInfo::process_cpu_jiffies();
    volatile uint64_t sink = 0;
    for (int i = 0; i < 50000000; ++i) sink += i;  // 忙循环烧 CPU（>1 jiffy=10ms）
    const int64_t j2 = fly::SystemInfo::process_cpu_jiffies();
    EXPECT_GE(j2, j1) << "进程 jiffies 单调不减";
    EXPECT_GT(j2, j1) << "5000 万次累加应至少消耗 1 jiffy CPU 时间";
    (void)sink;
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

    // 烧一点 CPU 再采：差分生效，各字段在合理域内。
    volatile uint64_t sink = 0;
    for (int i = 0; i < 20000000; ++i) sink += i;
    fly::MonitorSample second = sampler.sample_once();
    EXPECT_GT(second.epoch_ms_, first.epoch_ms_);
    EXPECT_LE(second.proc_cpu_bps_, 10000u);
    EXPECT_LE(second.host_cpu_bps_, 10000u);
    EXPECT_GT(second.proc_cpu_bps_, 0u) << "忙循环后进程 CPU% 应非零";
    EXPECT_GT(second.host_mem_total_bytes_, 0u);
    (void)sink;
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

}  // namespace
