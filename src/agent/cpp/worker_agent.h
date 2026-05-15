#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

namespace fly {

class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port);
    ~WorkerAgent();
    
    void start();
    void stop();
    bool is_running() const;
    uint64_t get_worker_id() const;
    
private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    bool running_;
};

}  // namespace fly