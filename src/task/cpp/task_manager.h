#pragma once

#include <common/cpp/common_types.h>
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

struct TaskMetadata {
    uint64_t task_id_ = 0;
    CMString name_;
    TaskStatus status_ = TaskStatus::PENDING;
    CMVector<CMString> inputs_;
    CMVector<CMString> outputs_;
    CMString config_;
    CMVector<CMString> required_capabilities_;
    float attribute_timeout_ = -1.0f;  // <0=死等, 0=立即降级, >0=限时降级
    int priority_ = 10;                // 任务优先级（worker 崩溃恢复时还原）
    uint64_t created_at_ = 0;
    uint64_t started_at_ = 0;
    uint64_t completed_at_ = 0;
    CMString error_message_;
    uint64_t assigned_worker_id_ = 0;
    CMString write_context_hash_;
};

using TaskMetadataPtr = CMSharedPtr<TaskMetadata>;

class TaskManager {
public:
    // ── Mutation ────────────────────────────────────────────────────
    void create_task(uint64_t task_id, const CMString& name,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& outputs,
                     const CMString& config,
                     const CMVector<CMString>& required_capabilities = {},
                     float attribute_timeout = -1.0f,
                     int priority = 10);

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
