#include <agent/cpp/peer_rpc_server.h>
#include <chrono>

namespace fly {

PeerRpcServer::PeerRpcServer() = default;

PeerRpcServer::~PeerRpcServer() {
    stop();
}

int PeerRpcServer::listen(const CMString& host, int port, RequestHandler handler) {
    if (running_.load()) {
        ERR("PeerRpcServer already running");
        return 0;
    }
    transport_ = create_connection_manager("tcp");
    if (!transport_) {
        ERR("PeerRpcServer: failed to create transport");
        return 0;
    }
    request_handler_ = std::move(handler);
    response_handler_ = nullptr;  // 服务端模式通常不收 response（但双向也可）
    try {
        transport_->listen(host, port);
    } catch (const std::exception& e) {
        ERR("PeerRpcServer listen failed: {}", e.what());
        return 0;
    }
    int bound_port = transport_->get_bound_port();
    if (bound_port <= 0) {
        ERR("PeerRpcServer: failed to get bound port");
        return 0;
    }
    running_.store(true);
    thread_ = std::thread(&PeerRpcServer::server_loop, this);
    INFO("PeerRpcServer listening on {}:{} running={}", host, bound_port, running_.load());
    return bound_port;
}

uint64_t PeerRpcServer::connect_peer(const CMString& host, int port,
                                      int retries, int retry_interval_ms) {
    if (!transport_) {
        // 仅客户端模式：仍需 transport（listen 未调过时创建）
        transport_ = create_connection_manager("tcp");
        if (!transport_) return 0;
        running_.store(true);
        thread_ = std::thread(&PeerRpcServer::server_loop, this);
    }
    for (int attempt = 0; attempt <= retries; attempt++) {
        uint64_t conn_id = transport_->connect(host, port);
        if (conn_id > 0) {
            DBG("PeerRpcServer connected to {}:{} conn_id={} (attempt {})", host, port, conn_id, attempt);
            return conn_id;
        }
        if (attempt < retries) {
            DBG("PeerRpcServer connect attempt {} failed, retrying in {}ms", attempt, retry_interval_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms));
        }
    }
    ERR("PeerRpcServer connect to {}:{} failed after {} retries", host, port, retries);
    return 0;
}

void PeerRpcServer::set_response_handler(ResponseHandler handler) {
    response_handler_ = std::move(handler);
}

void PeerRpcServer::server_loop() {
    INFO("PeerRpcServer server_loop started, running={}", running_.load());
    int poll_count = 0;
    while (running_.load()) {
        auto events = transport_->poll(10);  // 10ms timeout（频繁检查 DATA，避免被 CPU 密集 compute 饿死）
        poll_count++;
        if (!events.empty()) {
            INFO("PeerRpcServer poll#{} returned {} events", poll_count, events.size());
        }
        if (poll_count % 100 == 0) {
            INFO("PeerRpcServer still polling (count={})", poll_count);
        }
        for (auto& event : events) {
            switch (event.type_) {
                case TransportEventType::CONNECT: {
                    INFO("PeerRpcServer CONNECT conn_id={}", event.conn_id_);
                    std::lock_guard<std::mutex> lk(buf_mutex_);
                    recv_bufs_[event.conn_id_];
                    break;
                }
                case TransportEventType::DATA: {
                    CMString appended;
                    {
                        std::lock_guard<std::mutex> lk(buf_mutex_);
                        auto it = recv_bufs_.find(event.conn_id_);
                        if (it == recv_bufs_.end()) {
                            recv_bufs_[event.conn_id_];  // 首次（CONNECT 可能未到）
                            it = recv_bufs_.find(event.conn_id_);
                        }
                        it->second.append(event.data_);
                        // 循环切帧（一次可能收多个帧）
                        auto& buf = it->second;
                        while (true) {
                            // 帧完整性检查：4B total_len + 1B type + payload
                            if (buf.size() < 5) break;  // 不足 header，等更多数据
                            uint32_t total_len = MessageProtocol::get_total_size(buf);
                            if (total_len < 1 || buf.size() < 4 + total_len) break;  // 帧不完整
                            uint8_t raw_type = static_cast<uint8_t>(buf[4]);
                            if (raw_type == static_cast<uint8_t>(MessageType::PEER_RPC_REQUEST)) {
                                PeerRpcRequestMessage msg;
                                if (!MessageProtocol::decode(buf, msg)) {
                                    ERR("PeerRpcServer decode PEER_RPC_REQUEST failed, buf_size={}", buf.size());
                                    break;
                                }
                                INFO("PeerRpcServer decoded request rpc_id={} payload_size={}", msg.rpc_id_, msg.payload_.size());
                                if (request_handler_) {
                                    auto resp = request_handler_(event.conn_id_, msg.rpc_id_,
                                                                  msg.src_worker_id_, msg.payload_);
                                    if (resp.has_value()) {
                                        send_response(event.conn_id_, msg.rpc_id_, 0, resp.value());
                                    }
                                }
                            } else if (raw_type == static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE)) {
                                PeerRpcResponseMessage msg;
                                if (!MessageProtocol::decode(buf, msg)) break;
                                if (response_handler_) {
                                    response_handler_(event.conn_id_, msg.rpc_id_, msg.status_, msg.payload_);
                                }
                            } else {
                                buf.clear();  // 未知类型，清缓冲防积压
                                break;
                            }
                        }
                    }
                    break;
                }
                case TransportEventType::DISCONNECT: {
                    DBG("PeerRpcServer connection closed conn_id={}", event.conn_id_);
                    std::lock_guard<std::mutex> lk(buf_mutex_);
                    recv_bufs_.erase(event.conn_id_);
                    break;
                }
                default:  // ERROR or unknown
                    ERR("PeerRpcServer unknown event type={} conn_id={}",
                        static_cast<int>(event.type_), event.conn_id_);
                    break;
            }
        }
    }
}

bool PeerRpcServer::send_request(uint64_t conn_id, uint64_t rpc_id,
                                  uint64_t src_worker_id, const CMString& payload) {
    if (!transport_) {
        ERR("PeerRpcServer send_request: no transport");
        return false;
    }
    PeerRpcRequestMessage msg;
    msg.rpc_id_ = rpc_id;
    msg.src_worker_id_ = src_worker_id;
    msg.payload_ = payload;
    CMString frame = MessageProtocol::encode(msg);
    ssize_t result = transport_->send(conn_id, frame);
    return result > 0;
}

bool PeerRpcServer::send_response(uint64_t conn_id, uint64_t rpc_id,
                                   uint8_t status, const CMString& payload) {
    if (!transport_) return false;
    PeerRpcResponseMessage msg;
    msg.rpc_id_ = rpc_id;
    msg.status_ = status;
    msg.payload_ = payload;
    CMString frame = MessageProtocol::encode(msg);
    ssize_t result = transport_->send(conn_id, frame);
    return result > 0;
}

bool PeerRpcServer::notify_failure(uint64_t conn_id, const CMString& reason) {
    // notify_failure = status=1 的 response（无需对应 request，rpc_id=0）
    return send_response(conn_id, 0, 1, reason);
}

void PeerRpcServer::close_connection(uint64_t conn_id) {
    if (!transport_) return;
    transport_->close(conn_id);
    std::lock_guard<std::mutex> lk(buf_mutex_);
    recv_bufs_.erase(conn_id);
}

bool PeerRpcServer::is_connected(uint64_t conn_id) const {
    return transport_ && transport_->is_connected(conn_id);
}

void PeerRpcServer::stop() {
    running_.store(false);
    if (transport_) {
        transport_->stop_listening();
        transport_->close_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    {
        std::lock_guard<std::mutex> lk(buf_mutex_);
        recv_bufs_.clear();
    }
    transport_.reset();
    request_handler_ = nullptr;
    response_handler_ = nullptr;
    DBG("PeerRpcServer stopped");
}

}  // namespace fly
