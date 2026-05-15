#include <task/cpp/worker_manager.h>
#include <chrono>

namespace fly {

void WorkerManager::register_worker(uint64_t worker_id, const CMString& address, uint16_t port,
                                     const CMVector<CMString>& capabilities) {
    WorkerInfo info;
    info.worker_id = worker_id;
    info.address = address;
    info.port = port;
    info.status = WorkerStatus::IDLE;
    info.capabilities = capabilities;
    info.last_heartbeat = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info.current_task_id = 0;
    workers_[worker_id] = info;
}

void WorkerManager::unregister_worker(uint64_t worker_id) {
    workers_.erase(worker_id);
}

void WorkerManager::update_worker_status(uint64_t worker_id, WorkerStatus status) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.status = status;
    }
}

void WorkerManager::record_heartbeat(uint64_t worker_id) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.last_heartbeat = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

void WorkerManager::assign_task(uint64_t worker_id, uint64_t task_id) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.status = WorkerStatus::BUSY;
        it->second.current_task_id = task_id;
    }
}

void WorkerManager::complete_task(uint64_t worker_id) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.status = WorkerStatus::IDLE;
        it->second.current_task_id = 0;
    }
}

WorkerInfo* WorkerManager::get_worker(uint64_t worker_id) {
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        return &it->second;
    }
    return nullptr;
}

CMVector<uint64_t> WorkerManager::get_idle_workers() {
    CMVector<uint64_t> result;
    for (const auto& [id, info] : workers_) {
        if (info.status == WorkerStatus::IDLE) {
            result.push_back(id);
        }
    }
    return result;
}

CMVector<uint64_t> WorkerManager::get_workers_with_capability(const CMString& capability) {
    CMVector<uint64_t> result;
    for (const auto& [id, info] : workers_) {
        for (const auto& cap : info.capabilities) {
            if (cap == capability) {
                result.push_back(id);
                break;
            }
        }
    }
    return result;
}

CMVector<WorkerInfo> WorkerManager::get_all_workers() {
    CMVector<WorkerInfo> result;
    for (const auto& [id, info] : workers_) {
        result.push_back(info);
    }
    return result;
}

size_t WorkerManager::get_worker_count() {
    return workers_.size();
}

size_t WorkerManager::get_idle_worker_count() {
    size_t count = 0;
    for (const auto& [id, info] : workers_) {
        if (info.status == WorkerStatus::IDLE) {
            count++;
        }
    }
    return count;
}

}  // namespace fly