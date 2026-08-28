#include <task/cpp/heartbeat_monitor.h>

namespace fly {

HeartbeatMonitor::HeartbeatMonitor(WorkerManager* manager, uint64_t timeout_seconds)
    : manager_(manager), timeout_seconds_(timeout_seconds) {}

void HeartbeatMonitor::check_all_workers(uint64_t current_time,
                                         const CMVector<uint64_t>& exempt_workers) {
    dead_workers_.clear();

    auto all_workers = manager_->get_all_workers();
    for (const auto& worker : all_workers) {
        // 正常退出（EXITED，master 主动关停确认）不是心跳判死的产物：
        // 跳过且不进 dead 列表。
        if (worker.status_ == WorkerStatus::EXITED) {
            continue;
        }
        if (worker.status_ == WorkerStatus::DEAD) {
            dead_workers_.push_back(worker.worker_id_);
            continue;
        }

        // 断连宽限中的 worker（断连未判死）：心跳缺失是断连的自然结果——判死由
        // 宽限计时器统一负责（master heartbeat_check_loop 扫 grace_deadlines_），
        // 此处豁免，避免 120s 心跳超时与宽限窗口撞车抢跑。
        bool exempt = false;
        for (uint64_t id : exempt_workers) {
            if (id == worker.worker_id_) { exempt = true; break; }
        }
        if (exempt) continue;

        uint64_t elapsed = current_time - worker.last_heartbeat_;
        if (elapsed > timeout_seconds_) {
            manager_->update_worker_status(worker.worker_id_, WorkerStatus::DEAD);
            dead_workers_.push_back(worker.worker_id_);
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