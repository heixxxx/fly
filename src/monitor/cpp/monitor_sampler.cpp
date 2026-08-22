#include <monitor/cpp/monitor_sampler.h>
#include <core/cpp/system_info.h>
#include <network/cpp/net_stats.h>

#include <unistd.h>

namespace fly {

MonitorSampler::MonitorSampler() : hz_(sysconf(_SC_CLK_TCK)) {
    if (hz_ <= 0) hz_ = 100;
}

uint32_t MonitorSampler::ratio_bps(int64_t part, int64_t total) {
    if (total <= 0 || part < 0) return 0;
    int64_t bps = part * 10000 / total;
    if (bps > 10000) bps = 10000;  // 采样错位噪声钳制（本口径 100% = 全部核）
    return static_cast<uint32_t>(bps);
}

uint32_t MonitorSampler::proc_cpu_bps(int64_t d_proc, int64_t d_total) {
    return ratio_bps(d_proc, d_total);
}

uint32_t MonitorSampler::host_cpu_bps(int64_t d_total, int64_t d_idle) {
    if (d_total <= 0) return 0;
    // 非 idle 占比；d_idle > d_total（计数器回绕/采样错位）时钳 0。
    int64_t busy = d_total - d_idle;
    if (busy < 0) busy = 0;
    return ratio_bps(busy, d_total);
}

MonitorSample MonitorSampler::sample_once() {
    MonitorSample s;
    s.epoch_ms_ = monitor_epoch_ms_now();
    s.proc_rss_bytes_ = SystemInfo::process_rss_bytes();

    const HostMem hm = SystemInfo::host_mem_bytes();
    s.host_mem_total_bytes_ = hm.total_;
    s.host_mem_avail_bytes_ = hm.available_;

    const double load1 = SystemInfo::host_loadavg_1m();
    s.host_load1_x100_ = (load1 >= 0)
        ? static_cast<uint32_t>(load1 * 100.0 + 0.5) : 0;

    s.net_read_bytes_ = NetStats::instance().read_bytes();
    s.net_write_bytes_ = NetStats::instance().write_bytes();

    // CPU 差分：进程口径 dProc/dTotal（占总容量），host 口径非 idle 占比。
    // 差分状态互斥保护（多线程事件采样与周期采样共用本实例）。
    const int64_t proc_j = SystemInfo::process_cpu_jiffies();
    const SystemInfo::HostCpu hc = SystemInfo::host_cpu_jiffies();
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (has_baseline_ && proc_j >= 0 && hc.total_ > 0) {
            const int64_t d_proc = proc_j - prev_proc_jiffies_;
            const int64_t d_total = hc.total_ - prev_host_total_;
            const int64_t d_idle = hc.idle_ - prev_host_idle_;
            if (d_proc >= 0 && d_total > 0 && d_idle >= 0) {
                s.proc_cpu_bps_ = proc_cpu_bps(d_proc, d_total);
                s.host_cpu_bps_ = host_cpu_bps(d_total, d_idle);
            }
        }
        if (proc_j >= 0 && hc.total_ > 0) {
            prev_proc_jiffies_ = proc_j;
            prev_host_total_ = hc.total_;
            prev_host_idle_ = hc.idle_;
            has_baseline_ = true;
        }
    }
    return s;
}

}  // namespace fly
