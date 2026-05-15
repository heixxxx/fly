#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <agent/cpp/task_executor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>

namespace fly {

class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port);
    ~WorkerAgent();
    
    void start();
    void stop();
    bool is_running() const;
    uint64_t get_worker_id() const;
    
    void set_executor(TaskExecutor* executor);
    
    bool is_registered() const;
    
private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    
    std::unique_ptr<Reactor> reactor_;
    std::thread reactor_thread_;
    uint64_t master_conn_;
    
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    
    TaskExecutor* executor_{nullptr};
    
    void on_register_ack(const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    
    void heartbeat_loop();
};

}  // namespace fly