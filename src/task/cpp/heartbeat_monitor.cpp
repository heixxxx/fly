#include <task/cpp/heartbeat_monitor.h>

namespace fly {

HeartbeatMonitor::HeartbeatMonitor(WorkerManager* manager, uint64_t timeout_seconds)
    : manager_(manager), timeout_seconds_(timeout_seconds) {}

void HeartbeatMonitor::check_all_workers(uint64_t current_time) {
    dead_workers_.clear();
    
    auto all_workers = manager_->get_all_workers();
    for (const auto& worker : all_workers) {
        if (worker.status == WorkerStatus::DEAD) {
            dead_workers_.push_back(worker.worker_id);
            continue;
        }
        
        uint64_t elapsed = current_time - worker.last_heartbeat;
        if (elapsed > timeout_seconds_) {
            manager_->update_worker_status(worker.worker_id, WorkerStatus::DEAD);
            dead_workers_.push_back(worker.worker_id);
        }
    }
}

uint64_t HeartbeatMonitor::get_timeout() const {
    return timeout_seconds_;
}

void HeartbeatMonitor::set_timeout(uint64_t seconds) {
    timeout_seconds_ = seconds;
}

CMVector<uint64_t> HeartbeatMonitor::get_dead_workers() const {
    return dead_workers_;
}

}  // namespace fly