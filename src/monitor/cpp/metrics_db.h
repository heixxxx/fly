#pragma once

#include <monitor/cpp/monitor_types.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct sqlite3;

namespace fly {

// MetricsDb —— master 进程专属的监控数据 SQLite 单写器（cluster monitor 落盘层）。
//
// 并发模型（master 单写要求的具体化）：
//   · 所有 record_* 是非阻塞入队（互斥保护的任务队列，µs 级），可在任意线程
//     调用（reactor 并行 lane 上的 on_monitor_sample、调度路径上的 task 事件等）。
//   · sqlite3 访问完全收敛在唯一写线程：队列元素是「拿到 db 指针执行一条
//     prepared INSERT/UPSERT」的闭包，写线程排空队列合并为单个事务提交
//     （task 提交风暴千条级合并为一条事务，NFS 上不产生 fsync 风暴）。
//   · close() 在 MasterAgent::stop() 内同步调用：停写线程、排空余量、提交、
//     关闭连接。退出时序上早于 Logger 关闭与静态析构（P3-18 教训）。
//
// NFS 约束（见 docs/monitor-design.md 选型调研）：
//   · journal_mode=PERSIST（避免每事务建/删 journal 文件的 NFS 元数据抖动；
//     禁止 WAL——跨进程共享内存在 NFS 上不可用）。
//   · synchronous=NORMAL（减少 fsync；监控数据允许崩溃丢最后一批，单写者
//     不会损坏文件）。
//   · GUI 侧用 mode=ro 只读连接，与写者通过 POSIX 锁互斥（busy_timeout）。
//
// 数据保留：全量历史（终态 task 在 master 内存有 100 条 LRU 上限，DB 无上限，
// 事件发生时即写，天然保全量）。
class MetricsDb {
public:
    MetricsDb() = default;
    ~MetricsDb();

    MetricsDb(const MetricsDb&) = delete;
    MetricsDb& operator=(const MetricsDb&) = delete;

    // 打开 {log_dir}/monitor.db，建 schema，启动写线程。失败（目录不存在/
    // sqlite open 失败）返回 false 且保持未打开状态。重复调用安全（已开则忽略）。
    bool open(const CMString& log_dir);
    // 停写线程并同步 flush 全部余量后关闭连接（幂等）。未打开时无操作。
    void close();
    bool opened() const { return db_ != nullptr; }

    // ---- meta ----
    void record_run_meta(const CMString& key, const CMString& value);

    // ---- workers ----
    // worker 注册（upsert workers 全列 + REGISTER 事件）。
    void record_worker_registered(uint64_t worker_id, const CMString& hostname,
                                  const CMString& ip, const CMString& role,
                                  const CMString& attributes);
    // worker 生命周期事件（DEAD/REVIVED/DISCONNECT/...）：更新 workers.last_event
    // 并插入 events 流。
    void record_worker_event(uint64_t worker_id, const CMString& event,
                             const CMString& detail = "");

    // ---- samples ----
    // 一组成组样本（worker 心跳通道的 MonitorSampleMessage / master 自监控）。
    // 组内按 (worker_id, epoch_ms) 主键 INSERT OR IGNORE，重复补发幂等。
    void record_worker_samples(uint64_t worker_id, const CMVector<MonitorSample>& samples);

    // ---- tasks ----
    // task 行全量 UPSERT（master 内存态 + 消息扩展字段组合后的最新快照）。
    void record_task(const TaskRow& row);
    // task 事件流（SUBMIT/READY/ASSIGN/COMPLETE/FAIL/REQUEUE/...）。
    void record_task_event(uint64_t task_id, uint64_t worker_id, const CMString& event,
                           const CMString& detail = "");
    // 对象级 IO 明细（MonitorTaskIoMessage 一组）。
    void record_object_io(const CMVector<ObjectIoRecord>& records);

    // ---- 通用事件流 ----
    // category: task/worker/db/run/sched/storage。低频高价值事件统一落 events 表。
    void record_event(const CMString& category, const CMString& event,
                      uint64_t worker_id = 0, uint64_t task_id = 0,
                      const CMString& detail = "");

    // ---- 单测驱动 ----
    // 写线程当前队列长度（0 = 已全部提交）。测试用于等待入队被消化。
    size_t pending_count_for_testing();

private:
    using WriteOp = std::function<void(sqlite3*)>;

    // 入队一条写操作并唤醒写线程。
    void enqueue(WriteOp op);
    // 写线程主体：批量排空 → 单事务提交。
    void writer_loop();
    // 排空一批（swap 出队列后锁外执行）。返回提交的 op 数。
    size_t drain_batch();

    sqlite3* db_ = nullptr;
    CMString db_path_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<WriteOp> queue_;
    std::atomic<bool> writer_running_{false};
    std::thread writer_thread_;
};

}  // namespace fly
