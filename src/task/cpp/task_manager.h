#pragma once

#include <container/cpp/container_aliases.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <functional>

namespace fly {

enum class TaskStatus : uint8_t {
    PENDING = 0,
    RUNNING = 1,
    COMPLETED = 2,
    FAILED = 3,
    CANCELLED = 4,
};

inline constexpr int kMaxCompletedTasks = 100;

// TaskSubmissionSpec — task 提交时确定、全生命周期不可变的字段集合。
//
// 设计目的：消除 task 数据在多个结构（TaskMetadata / FailedTaskRecord /
// 网络消息）间的重复定义与手动逐字段复制。新增 task 提交字段时只需在此
// 一处定义 + 其 FLY_SERIALIZE，所有内嵌该 spec 的持有方自动获得该字段
// 并正确序列化，从结构上杜绝"加字段漏改某处"（如本次 priority bug：
// FailedTaskRecord 漏存、restart_failed_tasks 漏传）。
//
// 持有方通过组合（内嵌 TaskSubmissionSpec submission_）复用，而非各自
// 重复声明同名字段。FLY_FIELD 的 else 分支（s.object）原生支持嵌套
// 可序列化对象，父结构 FLY_SERIALIZE(submission_, ...) 即可整体序列化。
struct TaskSubmissionSpec {
    CMString name_;
    CMString module_;
    CMVector<CMString> args_;
    CMVector<CMString> inputs_;
    CMVector<CMString> outputs_;
    CMVector<CMString> required_capabilities_;
    float attribute_timeout_ = -1.0f;  // <0=死等, 0=立即降级, >0=限时降级
    int priority_ = 10;                // 任务优先级（数值越大越先调度）
    CMString write_context_hash_;      // 对象写入来源 provenance（运行时可被覆盖）
    CMVector<CMString> vars_;          // 声明的 var 全名列表（@as_task(vars=...)）
    // task 归属 db（开发规范：task 第一个参数必须是归属 db 对象）。空时由
    // MasterAgent::submit_task 从 args_ 的第一个 __fly_db__ 编码参数兜底推导；
    // 失败记录按此落盘 {owner_db_path}/failed_tasks.bin（见 DEVELOPMENT_GUIDELINES）。
    CMString owner_db_path_;

    FLY_SERIALIZE(name_, module_, args_, inputs_, outputs_,
                  required_capabilities_, attribute_timeout_, priority_,
                  write_context_hash_, vars_, owner_db_path_);
};

struct TaskMetadata {
    uint64_t task_id_ = 0;
    TaskSubmissionSpec submission_;    // 提交时确定的不变字段（单一来源）
    TaskStatus status_ = TaskStatus::PENDING;
    CMString config_;
    uint64_t created_at_ = 0;
    uint64_t started_at_ = 0;
    uint64_t completed_at_ = 0;
    CMString error_message_;
    uint64_t assigned_worker_id_ = 0;
};

using TaskMetadataPtr = CMSharedPtr<TaskMetadata>;

class TaskManager {
public:
    // ── Mutation ────────────────────────────────────────────────────
    // create_task 接收完整的 TaskSubmissionSpec，避免逐字段复制导致的漏传。
    void create_task(uint64_t task_id, const TaskSubmissionSpec& spec,
                     const CMString& config = "{}");

    void update_task_status(uint64_t task_id, TaskStatus status);
    void set_error(uint64_t task_id, const CMString& error);
    void set_assigned_worker(uint64_t task_id, uint64_t worker_id);
    void set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed);
    void set_write_context_hash(uint64_t task_id, const CMString& hash);

    // Atomic compound operations — single lock acquisition.
    void fail_task(uint64_t task_id, const CMString& error);
    void assign_task(uint64_t task_id, uint64_t worker_id);
    void unassign_task(uint64_t task_id);

    // ── Query ───────────────────────────────────────────────────────

    // Returns a shared_ptr — 0.2ns copy, data stays alive as long as caller holds it.
    TaskMetadataPtr get_task(uint64_t task_id) const;

    // Full metadata vector — prefer the lighter methods below when possible.
    CMVector<TaskMetadataPtr> get_tasks_by_status(TaskStatus status) const;
    CMVector<TaskMetadataPtr> get_all_tasks() const;

    // O(1) status checks.
    bool has_tasks_with_status(TaskStatus status) const;
    int count_tasks_by_status(TaskStatus status) const;

    // ID-only queries — O(k) where k = tasks with that status (not total tasks).
    CMVector<uint64_t> get_task_ids_by_status(TaskStatus status) const;
    CMVector<uint64_t> get_task_ids_by_worker(uint64_t worker_id) const;

    bool has_task(uint64_t task_id) const;
    void remove_task(uint64_t task_id);

private:
    void move_task(uint64_t task_id, TaskStatus from, TaskStatus to);
    void maybe_cleanup_completed();

    // Per-status buckets — each status owns its own map.
    // get_tasks_by_status() iterates only the target bucket, not all tasks.
    CMUnorderedMap<uint64_t, TaskMetadataPtr> buckets_[5];  // indexed by TaskStatus

    // Fast lookup: task_id → current status (avoids searching all buckets).
    CMUnorderedMap<uint64_t, TaskStatus> task_status_;

    mutable std::mutex mutex_;
};

}  // namespace fly
