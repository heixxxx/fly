#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <map>

namespace fly {

class MasterAgent {
public:
    MasterAgent(const CMString& host, uint16_t port);
    ~MasterAgent();
    
    void start();
    void stop();
    bool is_running() const;
    
    CMVector<uint64_t> get_connected_workers() const;
    size_t get_connection_count() const;
    
private:
    CMString host_;
    uint16_t port_;
    std::atomic<bool> running_{false};
    
    std::unique_ptr<Reactor> reactor_;
    std::thread reactor_thread_;
    
    CMMap<uint64_t, uint64_t> conn_to_worker_;
    CMMap<uint64_t, uint64_t> worker_to_conn_;
    
    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);
};

}  // namespace fly