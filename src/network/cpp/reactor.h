#pragma once

#include <network/cpp/transport.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/io_thread_pool.h>
#include <log/cpp/logger.h>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <memory>

namespace fly {

template<typename T>
using MessageHandler = std::function<void(uint64_t conn_id, const T& msg)>;

using GenericHandler = std::function<void(uint64_t conn_id, CMString& raw_msg)>;

class Reactor {
public:
    explicit Reactor(CMUniquePtr<TransportLayer> transport);
    ~Reactor();
    
    template<typename T>
    void register_handler(MessageHandler<T> handler);
    
    void on_connect(std::function<void(uint64_t)> handler);
    void on_disconnect(std::function<void(uint64_t)> handler);
    void on_error(std::function<void(uint64_t, int)> handler);
    
    void run();
    void run_once(int timeout_ms = 100);
    void stop();
    void wait_until_running() const;
    bool is_running() const { return running_.load(); }
    
    int get_bound_port() const { return transport_->get_bound_port(); }
    
    template<typename T>
    void send(uint64_t conn_id, const T& msg);
    
    uint64_t connect(const CMString& host, int port) {
        return transport_->connect(host, port);
    }
    
    void set_io_pool(CMSharedPtr<IOThreadPool> pool);
    CMSharedPtr<IOThreadPool> get_io_pool() const { return io_pool_; }

private:
    CMUniquePtr<TransportLayer> transport_;
    CMSharedPtr<IOThreadPool> io_pool_;
    
    CMUnorderedMap<uint64_t, CMString> recv_buffers_;
    
    CMUnorderedMap<MessageType, GenericHandler> handlers_;
    
    std::function<void(uint64_t)> connect_handler_;
    std::function<void(uint64_t)> disconnect_handler_;
    std::function<void(uint64_t, int)> error_handler_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    
    void handle_event(const TransportEvent& event);
    void dispatch_message(uint64_t conn_id, CMString& buffer);
};

template<typename T>
void Reactor::register_handler(MessageHandler<T> handler) {
    handlers_[T::msg_type] = [handler](uint64_t conn_id, CMString& raw) {
        T msg;
        if (MessageProtocol::decode(raw, msg)) {
            handler(conn_id, msg);
        }
    };
}

template<typename T>
void Reactor::send(uint64_t conn_id, const T& msg) {
    CMString frame = MessageProtocol::encode(msg);
    ssize_t result = transport_->send(conn_id, frame);
    if (result < 0) {
        WARN("Reactor::send failed for conn_id={}", conn_id);
    }
}

}  // namespace fly