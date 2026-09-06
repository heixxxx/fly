#pragma once

#include <container/cpp/container_aliases.h>
#include <task/cpp/worker_manager.h>
#include <cstdint>

namespace fly {

class HeartbeatMonitor {
public:
    HeartbeatMonitor(WorkerManager* manager, uint64_t timeout_seconds = 30);
    
    // exempt_workers：断连宽限中的 worker（心跳缺失由宽限计时器统一判定，豁免
    // 心跳超时检查，防 120s 心跳与宽限窗口撞车抢跑）。
    void check_all_workers(uint64_t current_time,
                           const CMVector<uint64_t>& exempt_workers = {});
    uint64_t get_timeout() const;
    void set_timeout(uint64_t seconds);
    CMVector<uint64_t> get_dead_workers() const;
    
private:
    WorkerManager* manager_;
    uint64_t timeout_seconds_;
    CMVector<uint64_t> dead_workers_;
};

}  // namespace fly