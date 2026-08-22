#pragma once

#include <monitor/cpp/monitor_types.h>

#include <mutex>

namespace fly {

// 差分式负载采样器：内部保留上次 jiffy 读数，sample_once() 产出一条完整
// MonitorSample。
//
// 线程模型：**线程安全**——任意线程可调 sample_once()（monitor 周期线程、
// reactor lane 上的事件采样、master 自监控共用一个实例）。差分状态由内部
// 互斥保护，采样全程 ~几十 µs，事件频率下锁竞争可忽略。事件驱动的调用方
// 需自行节流（见 WorkerAgent::sample_now_event）。
//
// 首次调用无基线（CPU% 记 0）；两次调用间隔过近导致差分分母为 0 时 CPU%
// 记 0（不外推）。
class MonitorSampler {
public:
    MonitorSampler();

    // 采一条完整样本（全部读 /proc + NetStats 快照，µs 级）。
    MonitorSample sample_once();

    // ---- 纯函数（单测覆盖边界）----
    // part/total 的百分比（bps，×100 整数定点）。total <= 0 或 part < 0 返回 0。
    static uint32_t ratio_bps(int64_t part, int64_t total);
    // 进程 CPU%：d_proc/d_total（吃满全部核 = 10000bps）。
    static uint32_t proc_cpu_bps(int64_t d_proc, int64_t d_total);
    // host CPU%：非 idle 占比。
    static uint32_t host_cpu_bps(int64_t d_total, int64_t d_idle);

private:
    long hz_ = 100;                 // sysconf(_SC_CLK_TCK)（构造时缓存）
    bool has_baseline_ = false;     // 首次采样无差分基线
    std::mutex mutex_;              // 差分状态保护（多线程事件采样）
    int64_t prev_proc_jiffies_ = 0;
    int64_t prev_host_total_ = 0;
    int64_t prev_host_idle_ = 0;
};

}  // namespace fly
