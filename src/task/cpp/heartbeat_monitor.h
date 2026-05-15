#pragma once

#include <common/cpp/common_types.h>
#include <task/cpp/worker_manager.h>
#include <cstdint>

namespace fly {

class HeartbeatMonitor {
public:
    HeartbeatMonitor(WorkerManager* manager, uint64_t timeout_seconds = 30);
    
    void check_all_workers(uint64_t current_time);
    uint64_t get_timeout() const;
    void set_timeout(uint64_t seconds);
    CMVector<uint64_t> get_dead_workers() const;
    
private:
    WorkerManager* manager_;
    uint64_t timeout_seconds_;
    CMVector<uint64_t> dead_workers_;
};

}  // namespace fly