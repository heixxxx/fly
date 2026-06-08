#pragma once

#include <network/cpp/transport.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/io_thread_pool.h>
#include <log/cpp/logger.h>
#include <functional>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>

namespace fly {

template<typename T>
using MessageHandler = std::function<void(uint64_t conn_id, const T& msg)>;

using GenericHandler = std::function<void(uint64_t conn_id, CMString& raw_msg)>;

class HandlerThreadPool {
public:
    explicit HandlerThreadPool(size_t num_threads, size_t max_queue_size = 100);
    ~HandlerThreadPool();

    bool submit(std::function<void()> task);
    void shutdown();
    bool is_shutdown() const { return stop_.load(); }

private:
    CMVector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    size_t max_queue_size_;

    void worker_loop();
};

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
    
    template<typename T>
    bool try_send(uint64_t conn_id, const T& msg);
    
    uint64_t connect(const CMString& host, int port) {
        return transport_->connect(host, port);
    }
    
    void set_io_pool(CMSharedPtr<IOThreadPool> pool);
    CMSharedPtr<IOThreadPool> get_io_pool() const { return io_pool_; }

    void set_handler_pool(CMUniquePtr<HandlerThreadPool> pool);
    HandlerThreadPool* get_handler_pool() { return handler_pool_.get(); }

private:
    CMUniquePtr<TransportLayer> transport_;
    CMSharedPtr<IOThreadPool> io_pool_;
    CMUniquePtr<HandlerThreadPool> handler_pool_;
    
    CMUnorderedMap<uint64_t, CMString> recv_buffers_;
    
    CMUnorderedMap<MessageType, GenericHandler> handlers_;
    
    std::function<void(uint64_t)> connect_handler_;
    std::function<void(uint64_t)> disconnect_handler_;
    std::function<void(uint64_t, int)> error_handler_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    std::mutex conn_send_mutex_map_mutex_;
    CMUnorderedMap<uint64_t, CMUniquePtr<std::mutex>> conn_send_mutexes_;
    std::mutex& get_send_mutex(uint64_t conn_id);
    void remove_send_mutex(uint64_t conn_id);
    
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
    std::lock_guard<std::mutex> lock(get_send_mutex(conn_id));
    ssize_t result = transport_->send(conn_id, frame);
    if (result < 0) {
        WARN("Reactor::send failed for conn_id={}", conn_id);
    }
}

template<typename T>
bool Reactor::try_send(uint64_t conn_id, const T& msg) {
    CMString frame = MessageProtocol::encode(msg);
    auto& mtx = get_send_mutex(conn_id);
    if (!mtx.try_lock()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mtx, std::adopt_lock);
    ssize_t result = transport_->send(conn_id, frame);
    if (result < 0) {
        WARN("Reactor::try_send failed for conn_id={}", conn_id);
        return false;
    }
    return true;
}

}  // namespace fly
