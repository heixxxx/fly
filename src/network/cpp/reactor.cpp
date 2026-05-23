#include <network/cpp/reactor.h>
#include <network/cpp/io_thread_pool.h>
#include <algorithm>

namespace fly {

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

void Reactor::handle_event(const TransportEvent& event) {
    switch (event.type) {
        case TransportEventType::CONNECT:
            recv_buffers_[event.conn_id] = "";
            if (connect_handler_) {
                connect_handler_(event.conn_id);
            }
            break;
            
        case TransportEventType::DATA:
            recv_buffers_[event.conn_id] += event.data;
            dispatch_message(event.conn_id, recv_buffers_[event.conn_id]);
            break;
            
        case TransportEventType::DISCONNECT:
            if (disconnect_handler_) {
                disconnect_handler_(event.conn_id);
            }
            recv_buffers_.erase(event.conn_id);
            break;
            
        case TransportEventType::ERROR:
            if (error_handler_) {
                error_handler_(event.conn_id, event.error_code);
            }
            recv_buffers_.erase(event.conn_id);
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
            break;
        }
    }
}

}  // namespace fly