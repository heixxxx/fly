#pragma once

#include <common/cpp/common_types.h>

#include <cstdint>

namespace fly {

// host 物理内存快照（字节，/proc/meminfo）。
struct HostMem {
    uint64_t total_ = 0;
    uint64_t free_ = 0;
    uint64_t available_ = 0;
};

// 收集并格式化 fly 启动时的基础信息（binary / 机器 / 网络 / 运行时），
// 供 FLY::0000 message 在 master/worker 启动时打印。
//
// 多行分组对齐排版，每行形如 "  field : value"，字段名对齐到固定宽度便于阅读。
// 各信息收集自 /proc、uname、gethostname、Config、ProcessInfo、build_info 等。
class SystemInfo {
public:
    // ---- 数值 API：机器信息定时日志（main.cpp resource_monitor_loop）与
    // RunMetricsCollector 采样共用。全部读 /proc，无缓存、无锁（各自读独立文件）。
    //
    // 当前进程物理内存 RSS（字节，/proc/self/status VmRSS）。读取失败返回 0。
    static uint64_t process_rss_bytes();
    // 当前进程物理内存历史峰值（字节，/proc/self/status VmHWM）。
    static uint64_t process_hwm_bytes();
    // host 物理内存（字节，/proc/meminfo 的 MemTotal/MemFree/MemAvailable）。
    static HostMem host_mem_bytes();
    // host 1 分钟综合负载（/proc/loadavg 首列）。读取失败返回 -1。
    static double host_loadavg_1m();

    // ---- CPU jiffies（monitor 采样差分用）----
    // 本进程 CPU 时间（jiffies，/proc/self/stat 的 utime+stime 之和；多线程
    // 进程为全部线程总和）。读取失败返回 -1。调用方对两次读数差分，除以
    // _SC_CLK_TCK 得 CPU 秒数（task cpu_time_ms 的口径）。
    static int64_t process_cpu_jiffies();
    // host CPU 时间快照（jiffies，/proc/stat 首行 cpu 汇总）。total = 全部
    // 时间片字段之和；idle = idle + iowait。读取失败返回 -1。
    // 差分口径：host CPU% = (dTotal - dIdle) / dTotal；
    //          进程 CPU%（占总容量）= dProc / dTotal（吃满全部核 = 100%）。
    struct HostCpu {
        int64_t total_ = -1;
        int64_t idle_ = -1;
    };
    static HostCpu host_cpu_jiffies();

    // 收集全部信息并返回排版后的多行文本（每行含换行）。
    // role: "master" 或 "worker"，标注当前进程角色。
    // listening_port: 实际监听端口（master 的 reactor 绑定端口 / worker 的 data server 端口），
    //   传 0 表示尚未绑定或不可用。
    // caller 负责逐行套上 [FLY::0000] 前缀后输出。
    static CMString format_startup_info(const CMString& role, int listening_port);

private:
    // 对齐辅助：返回 "  " + label 填充到 width + " : " + value + "\n"。
    static CMString align(const CMString& label, const CMString& value, size_t width = 18);
};

}  // namespace fly
