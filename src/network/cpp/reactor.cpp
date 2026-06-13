#include <network/cpp/reactor.h>
#include <network/cpp/io_thread_pool.h>
#include <log/cpp/logger.h>
#include <algorithm>

namespace fly {

HandlerThreadPool::HandlerThreadPool(size_t num_threads, size_t max_queue_size)
    : max_queue_size_(max_queue_size) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&HandlerThreadPool::worker_loop, this);
    }
}

HandlerThreadPool::~HandlerThreadPool() {
    shutdown();
}

void HandlerThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return stop_.load() || !tasks_.empty(); });
            if (stop_.load() && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

bool HandlerThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return false;
        if (tasks_.size() >= max_queue_size_) return false;
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void HandlerThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return;
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

Reactor::Reactor(CMUniquePtr<TransportLayer> transport)
    : transport_(std::move(transport)) {
}

Reactor::~Reactor() {
    stop();
}

void Reactor::on_connect(std::function<void(uint64_t)> handler) {
    connect_handler_ = handler;
}

void Reactor::on_disconnect(std::function<void(uint64_t)> handler) {
    disconnect_handler_ = handler;
}

void Reactor::on_error(std::function<void(uint64_t, int)> handler) {
    error_handler_ = handler;
}

void Reactor::run() {
    if (stop_requested_.load()) return;
    running_ = true;
    while (running_) {
        run_once(10);
        if (io_pool_) {
            io_pool_->process_completions();
        }
    }
}

void Reactor::wait_until_running() const {
    while (!running_.load()) {
        if (stop_requested_.load()) {
            ERR("Reactor::wait_until_running: stop requested before reactor started running");
            assert(false && "Reactor stop requested before running");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Reactor::run_once(int timeout_ms) {
    auto events = transport_->poll(timeout_ms);
    for (const auto& event : events) {
        handle_event(event);
    }
}

void Reactor::stop() {
    stop_requested_ = true;
    running_ = false;
}

void Reactor::set_io_pool(CMSharedPtr<IOThreadPool> pool) {
    io_pool_ = pool;
}

void Reactor::set_handler_pool(CMUniquePtr<HandlerThreadPool> pool) {
    handler_pool_ = std::move(pool);
}

std::mutex& Reactor::get_send_mutex(uint64_t conn_id) {
    {
        std::lock_guard<std::mutex> lock(conn_send_mutex_map_mutex_);
        auto it = conn_send_mutexes_.find(conn_id);
        if (it != conn_send_mutexes_.end()) {
            return *it->second;
        }
        auto m = CMMakeUnique<std::mutex>();
        auto& ref = *m;
        conn_send_mutexes_[conn_id] = std::move(m);
        return ref;
    }
}

void Reactor::remove_send_mutex(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(conn_send_mutex_map_mutex_);
    conn_send_mutexes_.erase(conn_id);
}

void Reactor::handle_event(const TransportEvent& event) {
    switch (event.type_) {
        case TransportEventType::CONNECT:
            recv_buffers_[event.conn_id_] = "";
            if (connect_handler_) {
                connect_handler_(event.conn_id_);
            }
            break;
            
        case TransportEventType::DATA:
            recv_buffers_[event.conn_id_] += event.data_;
            dispatch_message(event.conn_id_, recv_buffers_[event.conn_id_]);
            break;
            
        case TransportEventType::DISCONNECT:
            if (disconnect_handler_) {
                disconnect_handler_(event.conn_id_);
            }
            recv_buffers_.erase(event.conn_id_);
            remove_send_mutex(event.conn_id_);
            break;
            
        case TransportEventType::ERROR:
            if (error_handler_) {
                error_handler_(event.conn_id_, event.error_code_);
            }
            recv_buffers_.erase(event.conn_id_);
            remove_send_mutex(event.conn_id_);
            break;
    }
}

void Reactor::dispatch_message(uint64_t conn_id, CMString& buffer) {
    while (!buffer.empty()) {
        MessageType type = MessageProtocol::get_type(buffer);
        
        auto it = handlers_.find(type);
        if (it == handlers_.end()) {
            uint32_t total_size = MessageProtocol::get_total_size(buffer);
            if (total_size > 0 && buffer.size() >= 4 + total_size) {
                buffer.erase(0, 4 + total_size);
            } else {
                break;
            }
            continue;
        }
        
        CMString temp = buffer;
        auto& handler = it->second;
        handler(conn_id, buffer);
        
        if (buffer == temp) {
            ERR("dispatch_message: handler for type={} did not consume buffer, discarding {} bytes",
                static_cast<int>(type), buffer.size());
            buffer.clear();
        }
    }
}

}  // namespace fly
