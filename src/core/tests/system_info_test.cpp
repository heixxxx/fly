#include <gtest/gtest.h>
#include <core/cpp/system_info.h>

#include <fstream>
#include <string>

// 与 /proc/self/status 实读对照的辅助:返回指定 key 的 kB 值。
static uint64_t read_status_kb(const std::string& key) {
    std::ifstream ifs("/proc/self/status");
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.compare(0, key.size(), key) == 0) {
            // 形如 "VmRSS:\t  1234 kB"
            size_t start = line.find_first_of("0123456789");
            if (start == std::string::npos) return 0;
            return std::stoull(line.substr(start));
        }
    }
    return 0;
}

TEST(SystemInfoTest, ProcessRssBytesPositiveAndMatchesProc) {
    uint64_t rss = fly::SystemInfo::process_rss_bytes();
    // 任何活进程(gtest 进程含运行时)RSS 至少 1MB。
    EXPECT_GT(rss, 1024ull * 1024ull);

    // 与 /proc/self/status 实读对照。两次读取之间进程可能继续分配,
    // 容差 16MB 足够稳健(单测进程无大额并发分配)。
    uint64_t proc_kb = read_status_kb("VmRSS");
    EXPECT_NEAR(static_cast<double>(rss), static_cast<double>(proc_kb) * 1024.0,
                16.0 * 1024.0 * 1024.0);
}

TEST(SystemInfoTest, ProcessHwmNotBelowRss) {
    // VmHWM 是历史峰值,任何时刻 >= VmRSS(瞬时相等合法)。
    EXPECT_GE(fly::SystemInfo::process_hwm_bytes(), fly::SystemInfo::process_rss_bytes());
}

TEST(SystemInfoTest, HostMemBytesSane) {
    fly::HostMem m = fly::SystemInfo::host_mem_bytes();
    // CI/WSL 宿主机至少有数百 MB 内存。
    EXPECT_GT(m.total_, 256ull * 1024ull * 1024ull);
    EXPECT_LE(m.free_, m.total_);
    EXPECT_LE(m.available_, m.total_);
    // available 通常 >= free(available 含可回收缓存)。
    EXPECT_GE(m.available_, m.free_);
}

TEST(SystemInfoTest, HostLoadavgNonNegative) {
    // loadavg 1m 不会为负;读取失败返回 -1(本环境 /proc/loadavg 必存在)。
    EXPECT_GE(fly::SystemInfo::host_loadavg_1m(), 0.0);
}
