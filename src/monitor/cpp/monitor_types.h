#pragma once

#include <container/cpp/container_aliases.h>
#include <network/cpp/message_types.h>  // MonitorSample（消息与落盘共用单一定义）

#include <chrono>
#include <cstdint>

namespace fly {

// monitor 模块公共数据结构：task 行快照与对象级 IO 明细。
// 采样样本结构 MonitorSample 定义在 network/cpp/message_types.h（消息层），
// 本头文件经 include 提供给 MetricsDb 落盘层，字段口径单点维护。

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

// task 执行窗口的资源结算（worker 侧 TaskResourceTracker 产出，随
// TaskComplete/TaskFailedMessage 上报 master）。
struct TaskResourceAgg {
    uint64_t exec_start_ms_ = 0;          // worker 真实执行开始（区别 master 派发时刻）
    uint64_t exec_end_ms_ = 0;            // worker 真实执行结束
    uint64_t cpu_time_ms_ = 0;            // 窗口内进程 CPU 时间（utime+stime 差分）
    uint64_t mem_baseline_bytes_ = 0;     // 开始时进程 RSS 基线
    uint64_t mem_avg_bytes_ = 0;          // 窗口内 RSS 平均（含基线样本）
    uint64_t mem_peak_bytes_ = 0;         // 窗口内 RSS 峰值
    uint32_t sample_count_ = 0;           // 窗口内样本点数（≥2：begin/end 各一）
};

}  // namespace fly
