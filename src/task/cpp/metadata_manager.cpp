#include <task/cpp/metadata_manager.h>

namespace fly {

void MetadataManager::create_task(uint64_t task_id, const CMString& name,
                                   const CMVector<CMString>& inputs,
                                   const CMVector<CMString>& outputs,
                                   const CMString& config) {
    TaskMetadata meta;
    meta.task_id = task_id;
    meta.name = name;
    meta.status = TaskStatus::PENDING;
    meta.inputs = inputs;
    meta.outputs = outputs;
    meta.config = config;
    meta.created_at = 0;
    meta.started_at = 0;
    meta.completed_at = 0;
    meta.assigned_worker_id = 0;
    tasks_[task_id] = meta;
}

void MetadataManager::update_task_status(uint64_t task_id, TaskStatus status) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.status = status;
    }
}

void MetadataManager::set_error(uint64_t task_id, const CMString& error) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.error_message = error;
    }
}

void MetadataManager::set_assigned_worker(uint64_t task_id, uint64_t worker_id) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.assigned_worker_id = worker_id;
    }
}

void MetadataManager::set_timestamps(uint64_t task_id, uint64_t created, uint64_t started, uint64_t completed) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        if (created != 0) it->second.created_at = created;
        if (started != 0) it->second.started_at = started;
        if (completed != 0) it->second.completed_at = completed;
    }
}

TaskMetadata* MetadataManager::get_task(uint64_t task_id) {
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return &it->second;
    }
    return nullptr;
}

CMVector<TaskMetadata> MetadataManager::get_tasks_by_status(TaskStatus status) {
    CMVector<TaskMetadata> result;
    for (const auto& [id, meta] : tasks_) {
        if (meta.status == status) {
            result.push_back(meta);
        }
    }
    return result;
}

CMVector<TaskMetadata> MetadataManager::get_all_tasks() {
    CMVector<TaskMetadata> result;
    for (const auto& [id, meta] : tasks_) {
        result.push_back(meta);
    }
    return result;
}

bool MetadataManager::has_task(uint64_t task_id) {
    return tasks_.count(task_id) > 0;
}

void MetadataManager::remove_task(uint64_t task_id) {
    tasks_.erase(task_id);
}

void MetadataManager::record_data_location(const CMString& object_name, uint64_t worker_id) {
    object_to_worker_[object_name] = worker_id;
}

bool MetadataManager::has_data_location(const CMString& object_name) const {
    return object_to_worker_.count(object_name) > 0;
}

DataLocation MetadataManager::query_data_location(const CMString& object_name) const {
    auto it = object_to_worker_.find(object_name);
    if (it == object_to_worker_.end()) {
        return DataLocation{};
    }
    uint64_t wid = it->second;
    auto ds_it = worker_data_servers_.find(wid);
    if (ds_it != worker_data_servers_.end()) {
        return ds_it->second;
    }
    DataLocation loc;
    loc.worker_id = wid;
    return loc;
}

void MetadataManager::register_worker_data_server(uint64_t worker_id, const CMString& host, int32_t port) {
    DataLocation loc;
    loc.worker_id = worker_id;
    loc.data_host = host;
    loc.data_port = port;
    worker_data_servers_[worker_id] = loc;
}

DataLocation MetadataManager::get_worker_data_server(uint64_t worker_id) const {
    auto it = worker_data_servers_.find(worker_id);
    if (it != worker_data_servers_.end()) {
        return it->second;
    }
    return DataLocation{};
}

}  // namespace fly