#include <task/cpp/task_manager.h>
#include <chrono>

namespace fly {

void TaskManager::create_task(uint64_t task_id, const CMString& name,
                                    const CMVector<CMString>& inputs,
                                    const CMVector<CMString>& outputs,
                                    const CMString& config,
                                    const CMVector<CMString>& required_capabilities) {
    std::lock_guard<std::mutex> lock(mutex_);
    TaskMetadata meta;
    meta.task_id_ = task_id;
    meta.name_ = name;
    meta.status_ = TaskStatus::PENDING;
    meta.inputs_ = inputs;
    meta.outputs_ = outputs;
    meta.config_ = config;
    meta.required_capabilities_ = required_capabilities;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    meta.created_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    meta.started_at_ = 0;
    meta.completed_at_ = 0;
    meta.assigned_worker_id_ = 0;
    tasks_[task_id] = meta;
}

void TaskManager::update_task_status(uint64_t task_id, TaskStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.status_ = status;
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        if (status == TaskStatus::RUNNING && it->second.started_at_ == 0) {
            it->second.started_at_ = ms;
        }
        if (status == TaskStatus::COMPLETED || status == TaskStatus::FAILED) {
            it->second.completed_at_ = ms;
        }
    }
}

void TaskManager::set_error(uint64_t task_id, const CMString& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.error_message_ = error;
    }
}

void TaskManager::set_assigned_worker(uint64_t task_id, uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.assigned_worker_id_ = worker_id;
    }
}

void TaskManager::set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        if (created != 0) it->second.created_at_ = created;
        if (started != 0) it->second.started_at_ = started;
        if (completed != 0) it->second.completed_at_ = completed;
    }
}

std::optional<std::reference_wrapper<TaskMetadata>> TaskManager::get_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}

CMVector<TaskMetadata> TaskManager::get_tasks_by_status(TaskStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<TaskMetadata> result;
    for (const auto& [id, meta] : tasks_) {
        if (meta.status_ == status) {
            result.push_back(meta);
        }
    }
    return result;
}

CMVector<TaskMetadata> TaskManager::get_all_tasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<TaskMetadata> result;
    for (const auto& [id, meta] : tasks_) {
        result.push_back(meta);
    }
    return result;
}

bool TaskManager::has_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.count(task_id) > 0;
}

void TaskManager::remove_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.erase(task_id);
}

}  // namespace fly
