#pragma once

#include <common/cpp/common_types.h>

#include <chrono>
#include <cstdint>

namespace fly {

// monitor 模块公共数据结构：worker/master 周期采样样本与 task 行快照。
// 采样样本同时用于网络消息（MonitorSampleMessage）与 MetricsDb 落盘
// （worker_samples 表），字段口径在此单点定义。

// 一条进程负载采样（worker 侧 monitor_report_loop 每采样周期产一条；
// master 侧自监控采样线程同构直写 DB）。
//
// 数值口径：
//   · proc_cpu_bps / host_cpu_bps：CPU 百分比 × 100（basis points，整数定点，
//     规避浮点序列化）。proc 口径 = 进程 jiffies 增量 / 机器总 jiffies 增量，
//     10000 = 吃满全部核（与 main.cpp MachineInfo 日志同口径）。
//   · host_load1_x100：1 分钟 loadavg × 100。
//   · net_read_bytes / net_write_bytes：本进程 TCP 累计字节数（NetStats，
//     单调递增；GUI 侧对相邻样本差分得到速率，计数器回绕/进程重启由
//     单调性检查兜底）。
struct MonitorSample {
    uint64_t epoch_ms_ = 0;               // unix epoch 毫秒（真实采样时刻）
    uint64_t proc_rss_bytes_ = 0;         // 本进程物理内存 RSS
    uint32_t proc_cpu_bps_ = 0;           // 本进程 CPU%（×100）
    uint32_t host_cpu_bps_ = 0;           // 机器总 CPU%（×100）
    uint64_t host_mem_total_bytes_ = 0;   // host MemTotal
    uint64_t host_mem_avail_bytes_ = 0;   // host MemAvailable
    uint32_t host_load1_x100_ = 0;        // host loadavg 1m（×100）
    uint64_t net_read_bytes_ = 0;         // 本进程网络累计读字节
    uint64_t net_write_bytes_ = 0;        // 本进程网络累计写字节
};

// tasks 表一行的全量快照。master 侧组合 TaskMetadata + 消息扩展字段后
// UPSERT 写入；status 单向迁移（PENDING→RUNNING→终态，REQUEUE 回 PENDING），
// 全列覆盖即最新状态，历史变迁由 events 表的事件流承载。
struct TaskRow {
    uint64_t task_id_ = 0;
    CMString name_;
    CMString module_;
    bool is_internal_ = false;
    CMString status_;           // PENDING/RUNNING/COMPLETED/FAILED/CANCELLED
    uint64_t worker_id_ = 0;
    int32_t priority_ = 0;
    CMString error_;
    uint64_t created_ms_ = 0;   // master submit 时刻
    uint64_t ready_ms_ = 0;     // 依赖满足时刻（0 = 未知）
    uint64_t started_ms_ = 0;   // master assign 时刻
    uint64_t completed_ms_ = 0; // master 收到终态时刻
    uint64_t exec_start_ms_ = 0;  // worker 真实执行开始
    uint64_t exec_end_ms_ = 0;    // worker 真实执行结束
    uint64_t cpu_time_ms_ = 0;    // 执行窗口内进程 CPU 时间（utime+stime 差分）
    uint64_t read_time_ms_ = 0;   // read_object 累计耗时
    uint64_t write_time_ms_ = 0;  // write_object + drain 落盘累计耗时
    uint64_t read_bytes_ = 0;     // 解压后读字节（pickle 路径精确，C++ 对象路径 0）
    uint64_t write_bytes_ = 0;    // 压缩后写字节（WriteRecord.size_bytes_ 汇总）
    uint64_t mem_baseline_bytes_ = 0;  // task 开始时进程 RSS 基线
    uint64_t mem_avg_bytes_ = 0;       // 执行窗口内进程 RSS 平均
    uint64_t mem_peak_bytes_ = 0;      // 执行窗口内进程 RSS 峰值
    CMString dbs_;             // 关联 db 路径（逗号分隔，submit 时解析 args/inputs/outputs）
};

// 对象级 IO 明细一条（worker 侧 read_object/write_object 单次调用）。
struct ObjectIoRecord {
    uint64_t epoch_ms_ = 0;
    uint64_t task_id_ = 0;
    uint64_t worker_id_ = 0;
    bool is_write_ = false;      // false = read, true = write
    CMString object_name_;       // 对象全名 "db_path:short_name"
    uint64_t bytes_ = 0;         // read=解压后字节；write=压缩后字节（0=不可得）
    uint64_t duration_ms_ = 0;   // 本次调用耗时
};

// 当前时间 unix epoch 毫秒（与 RunMetricsCollector::epoch_ms_now 同口径）。
inline uint64_t monitor_epoch_ms_now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

}  // namespace fly
