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

struct TaskMetadata {
    uint64_t task_id_;
    CMString name_;
    TaskStatus status_;
    CMVector<CMString> inputs_;
    CMVector<CMString> outputs_;
    CMString config_;
    CMVector<CMString> required_capabilities_;
    uint64_t created_at_;
    uint64_t started_at_;
    uint64_t completed_at_;
    CMString error_message_;
    uint64_t assigned_worker_id_;
    CMString write_context_hash_;
};

class TaskManager {
public:
    void create_task(uint64_t task_id, const CMString& name,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& outputs,
                     const CMString& config,
                     const CMVector<CMString>& required_capabilities = {});
    void update_task_status(uint64_t task_id, TaskStatus status);
    void set_error(uint64_t task_id, const CMString& error);
    void set_assigned_worker(uint64_t task_id, uint64_t worker_id);
    void set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed);

    std::optional<std::reference_wrapper<TaskMetadata>> get_task(uint64_t task_id);
    CMVector<TaskMetadata> get_tasks_by_status(TaskStatus status);
    CMVector<TaskMetadata> get_all_tasks();
    bool has_task(uint64_t task_id);
    void remove_task(uint64_t task_id);

private:
    CMUnorderedMap<uint64_t, TaskMetadata> tasks_;
    mutable std::mutex mutex_;
};

}  // namespace fly
