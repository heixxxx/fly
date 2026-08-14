#include <task/cpp/worker_manager.h>
#include <log/cpp/logger.h>
#include <algorithm>
#include <chrono>
#include <sstream>

namespace fly {

void WorkerManager::register_worker(uint64_t worker_id, const CMString& address,
                                      uint16_t port, const CMVector<CMString>& capabilities,
                                      const CMString& hostname, const CMString& ip_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    // worker 重连会用相同 worker_id 重新注册（合法）。但若旧 worker 仍处 BUSY（带着
    // 未完成 task），静默覆盖成 IDLE 会丢失 task 关联 → task 永久孤儿。此处 WARN 暴露
    // 这种异常重注册，便于及时发现（重连应先经 on_disconnect 清理旧状态）。
    auto it = workers_.find(worker_id);
    if (it != workers_.end() && it->second.status_ == WorkerStatus::BUSY) {
        WARN("[WORKER-DUP] register_worker: worker_id={} re-registered while BUSY "
             "(current_task_id={} lost) — possible unclean reconnect",
             worker_id, it->second.current_task_id_);
    }
    WorkerInfo info;
    info.worker_id_ = worker_id;
    info.address_ = address;
    info.port_ = port;
    info.status_ = WorkerStatus::IDLE;
    info.capabilities_ = capabilities;
    info.last_heartbeat_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info.current_task_id_ = 0;
    info.hostname_ = hostname;
    info.ip_address_ = ip_address;
    workers_[worker_id] = info;
}

void WorkerManager::unregister_worker(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    workers_.erase(worker_id);
}

void WorkerManager::update_worker_status(uint64_t worker_id, WorkerStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.status_ = status;
    }
}

void WorkerManager::record_heartbeat(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.last_heartbeat_ = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

void WorkerManager::set_heartbeat(uint64_t worker_id, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.last_heartbeat_ = timestamp;
    }
}

void WorkerManager::assign_task(uint64_t worker_id, uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.status_ = WorkerStatus::BUSY;
        it->second.current_task_id_ = task_id;
    }
}

void WorkerManager::complete_task(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.status_ = WorkerStatus::IDLE;
        it->second.current_task_id_ = 0;
    }
}

void WorkerManager::cancel_task_if_assigned(uint64_t worker_id, uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end() && it->second.current_task_id_ == task_id &&
        it->second.status_ == WorkerStatus::BUSY) {
        it->second.status_ = WorkerStatus::IDLE;
        it->second.current_task_id_ = 0;
    }
}

void WorkerManager::update_capabilities(uint64_t worker_id,
                                          const CMVector<CMString>& added,
                                          const CMVector<CMString>& removed) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it == workers_.end()) return;

    auto& caps = it->second.capabilities_;

    for (const auto& prop : added) {
        bool exists = false;
        for (const auto& c : caps) {
            if (c == prop) { exists = true; break; }
        }
        if (!exists) {
            caps.push_back(prop);
        }
    }

    for (const auto& prop : removed) {
        auto cit = std::find(caps.begin(), caps.end(), prop);
        if (cit != caps.end()) {
            caps.erase(cit);
        }
    }
}

void WorkerManager::set_hostname(uint64_t worker_id, const CMString& hostname, const CMString& ip_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        it->second.hostname_ = hostname;
        if (!ip_address.empty()) {
            it->second.ip_address_ = ip_address;
        }
    }
}

CMString WorkerManager::get_hostname(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        return it->second.hostname_;
    }
    return "";
}

CMString WorkerManager::get_ip_address(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        return it->second.ip_address_;
    }
    return "";
}

std::optional<std::reference_wrapper<WorkerInfo>> WorkerManager::get_worker(uint64_t worker_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = workers_.find(worker_id);
    if (it != workers_.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}

CMVector<uint64_t> WorkerManager::get_idle_workers() {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<uint64_t> result;
    for (const auto& [id, info] : workers_) {
        if (info.status_ == WorkerStatus::IDLE) {
            result.push_back(id);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

CMVector<uint64_t> WorkerManager::get_workers_with_capability(const CMString& capability) {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<uint64_t> result;
    for (const auto& [id, info] : workers_) {
        for (const auto& cap : info.capabilities_) {
            if (cap == capability) {
                result.push_back(id);
                break;
            }
        }
    }
    return result;
}

CMVector<WorkerInfo> WorkerManager::get_all_workers() {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<WorkerInfo> result;
    for (const auto& [id, info] : workers_) {
        result.push_back(info);
    }
    return result;
}

bool WorkerManager::has_worker_with_all_capabilities(const CMVector<CMString>& capabilities) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (capabilities.empty()) return true;

    for (const auto& [id, info] : workers_) {
        bool has_all = true;
        for (const auto& req : capabilities) {
            bool found = false;
            for (const auto& cap : info.capabilities_) {
                if (cap == req) { found = true; break; }
            }
            if (!found) { has_all = false; break; }
        }
        if (has_all) return true;
    }
    return false;
}

size_t WorkerManager::get_worker_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

size_t WorkerManager::get_idle_worker_count() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& [id, info] : workers_) {
        if (info.status_ == WorkerStatus::IDLE) {
            count++;
        }
    }
    return count;
}

CMString WorkerManager::debug_worker_status() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    oss << "workers=" << workers_.size() << " ";
    for (const auto& [id, info] : workers_) {
        const char* st = (info.status_ == WorkerStatus::IDLE)   ? "IDLE"
                        : (info.status_ == WorkerStatus::BUSY)  ? "BUSY"
                        : "OTHER";
        oss << "[w" << id << "=" << st << " caps=" << info.capabilities_.size()
            << " task=" << info.current_task_id_ << "] ";
    }
    return oss.str();
}

}  // namespace fly
