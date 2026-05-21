#include <task/cpp/task_manager.h>

namespace fly {

void TaskManager::create_task(uint64_t task_id, const CMString& name,
                                    const CMVector<CMString>& inputs,
                                    const CMVector<CMString>& outputs,
                                    const CMString& config,
                                    const CMVector<CMString>& required_capabilities) {
    TaskMetadata meta;
    meta.task_id = task_id;
    meta.name = name;
    meta.status = TaskStatus::PENDING;
    meta.inputs = inputs;
    meta.outputs = outputs;
    meta.config = config;
    meta.required_capabilities = required_capabilities;
    meta.created_at = 0;
    meta.started_at = 0;
    meta.completed_at = 0;
    meta.assigned_worker_id = 0;
    tasks_[task_id] = meta;
}

void TaskManager::update_task_status(uint64_t task_id, TaskStatus status) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.status = status;
    }
}

void TaskManager::set_error(uint64_t task_id, const CMString& error) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.error_message = error;
    }
}

void TaskManager::set_assigned_worker(uint64_t task_id, uint64_t worker_id) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.assigned_worker_id = worker_id;
    }
}

void TaskManager::set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        if (created != 0) it->second.created_at = created;
        if (started != 0) it->second.started_at = started;
        if (completed != 0) it->second.completed_at = completed;
    }
}

TaskMetadata* TaskManager::get_task(uint64_t task_id) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return &it->second;
    }
    return nullptr;
}

CMVector<TaskMetadata> TaskManager::get_tasks_by_status(TaskStatus status) {
    CMVector<TaskMetadata> result;
    for (const auto& [id, meta] : tasks_) {
        if (meta.status == status) {
            result.push_back(meta);
        }
    }
    return result;
}

CMVector<TaskMetadata> TaskManager::get_all_tasks() {
    CMVector<TaskMetadata> result;
    for (const auto& [id, meta] : tasks_) {
        result.push_back(meta);
    }
    return result;
}

bool TaskManager::has_task(uint64_t task_id) {
    return tasks_.count(task_id) > 0;
}

void TaskManager::remove_task(uint64_t task_id) {
    tasks_.erase(task_id);
}

}  // namespace fly
