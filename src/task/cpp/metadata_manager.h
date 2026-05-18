#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

namespace fly {

enum class TaskStatus : uint8_t {
    PENDING = 0,
    RUNNING = 1,
    COMPLETED = 2,
    FAILED = 3,
    CANCELLED = 4,
};

struct TaskMetadata {
    uint64_t task_id;
    CMString name;
    TaskStatus status;
    CMVector<CMString> inputs;
    CMVector<CMString> outputs;
    CMString config;
    uint64_t created_at;
    uint64_t started_at;
    uint64_t completed_at;
    CMString error_message;
    uint64_t assigned_worker_id;
};

class MetadataManager {
public:
    void create_task(uint64_t task_id, const CMString& name,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& outputs,
                     const CMString& config);
    void update_task_status(uint64_t task_id, TaskStatus status);
    void set_error(uint64_t task_id, const CMString& error);
    void set_assigned_worker(uint64_t task_id, uint64_t worker_id);
    void set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed);

    TaskMetadata* get_task(uint64_t task_id);
    CMVector<TaskMetadata> get_tasks_by_status(TaskStatus status);
    CMVector<TaskMetadata> get_all_tasks();
    bool has_task(uint64_t task_id);
    void remove_task(uint64_t task_id);

private:
    CMMap<uint64_t, TaskMetadata> tasks_;
};

}  // namespace fly
