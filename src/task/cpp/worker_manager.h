#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

namespace fly {

enum class WorkerStatus : uint8_t {
    IDLE = 0,
    BUSY = 1,
    DEAD = 2,
};

struct WorkerInfo {
    uint64_t worker_id;
    CMString address;
    uint16_t port;
    WorkerStatus status;
    CMVector<CMString> capabilities;
    uint64_t last_heartbeat;
    uint64_t current_task_id;
};

class WorkerManager {
public:
    void register_worker(uint64_t worker_id, const CMString& address, uint16_t port,
                          const CMVector<CMString>& capabilities = {});
    void register_worker(uint64_t worker_id, const CMString& address,
                          const CMVector<CMString>& capabilities);
    void unregister_worker(uint64_t worker_id);
    void update_worker_status(uint64_t worker_id, WorkerStatus status);
    void record_heartbeat(uint64_t worker_id);
    void set_heartbeat(uint64_t worker_id, uint64_t timestamp);
    void assign_task(uint64_t worker_id, uint64_t task_id);
    void complete_task(uint64_t worker_id);
    
    WorkerInfo* get_worker(uint64_t worker_id);
    CMVector<uint64_t> get_idle_workers();
    CMVector<uint64_t> get_workers_with_capability(const CMString& capability);
    CMVector<WorkerInfo> get_all_workers();
    size_t get_worker_count();
    size_t get_idle_worker_count();
    
private:
    CMMap<uint64_t, WorkerInfo> workers_;
};

}  // namespace fly