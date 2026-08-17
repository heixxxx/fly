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
    if (!transport_->listen(host, port)) {
        ERR("PeerRpcServer listen failed on {}:{}", host, port);
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

void PeerRpcServer::set_disconnect_handler(DisconnectHandler handler) {
    disconnect_handler_ = std::move(handler);
}

void PeerRpcServer::server_loop() {
    while (running_.load()) {
        auto events = transport_->poll(10);  // 10ms timeout
        for (auto& event : events) {
            switch (event.type_) {
                case TransportEventType::CONNECT: {
                    DBG("PeerRpcServer CONNECT conn_id={}", event.conn_id_);
                    std::lock_guard<std::mutex> lk(buf_mutex_);
                    recv_bufs_[event.conn_id_];
                    break;
                }
                case TransportEventType::DATA: {
                    // 锁内只做 append + 切帧（decode），handler 回调移到锁外，
                    // 避免慢回调阻塞其他连接的接收。
                    struct DecodedMsg {
                        bool is_request;         // true=REQUEST, false=RESPONSE
                        uint64_t rpc_id;
                        uint64_t src_worker_id;  // REQUEST only
                        uint8_t status;          // RESPONSE only
                        CMString payload;
                    };
                    CMVector<DecodedMsg> decoded_msgs;
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
                                // decode 失败时 buf 不会被消费（erase 只在成功时执行），
                                // 会导致下次循环读到同一坏帧 → 无限循环。
                                // 改为：丢弃坏帧并 WARN，继续处理后续帧。
                                if (!MessageProtocol::decode(buf, msg)) {
                                    WARN("PeerRpcServer: corrupt REQUEST frame ({}B), discarding",
                                         4 + total_len);
                                    buf.erase(0, 4 + total_len);
                                    continue;
                                }
                                decoded_msgs.push_back({true, msg.rpc_id_, msg.src_worker_id_,
                                                        0, std::move(msg.payload_)});
                            } else if (raw_type == static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE)) {
                                PeerRpcResponseMessage msg;
                                if (!MessageProtocol::decode(buf, msg)) {
                                    WARN("PeerRpcServer: corrupt RESPONSE frame ({}B), discarding",
                                         4 + total_len);
                                    buf.erase(0, 4 + total_len);
                                    continue;
                                }
                                decoded_msgs.push_back({false, msg.rpc_id_, 0,
                                                        msg.status_, std::move(msg.payload_)});
                            } else {
                                buf.clear();  // 未知类型，清缓冲防积压
                                break;
                            }
                        }
                    }
                    // 锁外调 handler（回调可能耗时，不应持锁）
                    for (auto& dm : decoded_msgs) {
                        if (dm.is_request) {
                            if (request_handler_) {
                                auto resp = request_handler_(event.conn_id_, dm.rpc_id,
                                                              dm.src_worker_id, dm.payload);
                                if (resp.has_value()) {
                                    send_response(event.conn_id_, dm.rpc_id,
                                                   static_cast<uint8_t>(PeerRpcWireStatus::OK),
                                                   resp.value());
                                }
                            }
                        } else {
                            // BYE 握手：status=BYE 是连接管理信号，
                            // 不走 response_handler（不传到 pending RPC）。
                            if (dm.status == static_cast<uint8_t>(PeerRpcWireStatus::BYE)) {
                                handle_bye(event.conn_id_);
                            } else if (response_handler_) {
                                response_handler_(event.conn_id_, dm.rpc_id, dm.status, dm.payload);
                            }
                        }
                    }
                    break;
                }
                case TransportEventType::DISCONNECT: {
                    DBG("PeerRpcServer connection closed conn_id={}", event.conn_id_);
                    {
                        std::lock_guard<std::mutex> lk(buf_mutex_);
                        recv_bufs_.erase(event.conn_id_);
                    }
                    // BYE 握手区分：已标记 bye_closed 的 conn 是正常关闭，静默；
                    // 否则是错误断连（崩溃/网络断），触发 disconnect_handler 通知调用方。
                    // 同时唤醒 send_bye 的 wait（对端关了，BYE_ACK 不会来了）。
                    bool is_bye;
                    {
                        std::lock_guard<std::mutex> lk(bye_mutex_);
                        is_bye = bye_closed_conns_.erase(event.conn_id_) > 0;
                        bye_pending_conns_.erase(event.conn_id_);
                    }
                    bye_cv_.notify_all();
                    if (!is_bye && disconnect_handler_) {
                        disconnect_handler_(event.conn_id_);
                    }
                    break;
                }
                default:  // ERROR
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
    // notify_failure = NOTIFY_FAILURE 的 response（无需对应 request，rpc_id=0）
    return send_response(conn_id, 0,
                          static_cast<uint8_t>(PeerRpcWireStatus::NOTIFY_FAILURE), reason);
}

void PeerRpcServer::close_connection(uint64_t conn_id) {
    if (!transport_) return;
    transport_->close(conn_id);
    std::lock_guard<std::mutex> lk(buf_mutex_);
    recv_bufs_.erase(conn_id);
}

void PeerRpcServer::handle_bye(uint64_t conn_id) {
    // 服务端收到客户端 BYE：回 BYE_ACK + close + 标记。
    // 双方都用 PeerRpcWireStatus::BYE 的 PeerRpcResponse 表示。
    bool is_client_bye;  // 本端是否是客户端（已发 BYE 待 ACK）
    {
        std::lock_guard<std::mutex> lk(bye_mutex_);
        is_client_bye = bye_pending_conns_.erase(conn_id) > 0;
    }
    if (is_client_bye) {
        // 客户端收到服务端回的 BYE_ACK：唤醒 send_bye 的 wait。
        {
            std::lock_guard<std::mutex> lk(bye_mutex_);
            bye_ack_conns_.insert(conn_id);
        }
        bye_cv_.notify_all();
    } else {
        // 服务端收到客户端 BYE：回 BYE_ACK + close + 标记正常关闭。
        DBG("PeerRpcServer BYE received conn_id={}, sending BYE_ACK + close", conn_id);
        send_response(conn_id, 0,
                       static_cast<uint8_t>(PeerRpcWireStatus::BYE), "");  // BYE_ACK
        {
            std::lock_guard<std::mutex> lk(bye_mutex_);
            bye_closed_conns_.insert(conn_id);
        }
        close_connection(conn_id);
    }
}

bool PeerRpcServer::send_bye(uint64_t conn_id) {
    // 客户端：发 BYE → 同步等服务端回 BYE_ACK（5s 超时）→ close。
    // BYE 丢失或超时时直接 close（DISCONNECT 兜底）。
    {
        std::lock_guard<std::mutex> lk(bye_mutex_);
        bye_pending_conns_.insert(conn_id);
    }
    DBG("PeerRpcServer sending BYE conn_id={}", conn_id);
    send_response(conn_id, 0,
                   static_cast<uint8_t>(PeerRpcWireStatus::BYE), "");

    // 等服务端回 BYE_ACK（bye_ack_conns），或 DISCONNECT 发生（bye_closed / bye_pending 被 erase）。
    std::unique_lock<std::mutex> lk(bye_mutex_);
    bye_cv_.wait_for(lk, std::chrono::seconds(5), [&] {
        return bye_ack_conns_.count(conn_id) > 0 || bye_pending_conns_.count(conn_id) == 0;
    });
    bool got_ack = bye_ack_conns_.erase(conn_id) > 0;
    bye_pending_conns_.erase(conn_id);
    bye_closed_conns_.insert(conn_id);  // 标记正常关闭（DISCONNECT 时静默）
    lk.unlock();

    if (got_ack) {
        DBG("PeerRpcServer BYE_ACK received conn_id={}, closing", conn_id);
    } else {
        WARN("PeerRpcServer BYE timeout (no ACK), force close conn_id={}", conn_id);
    }
    close_connection(conn_id);  // 幂等（服务端可能已 close）
    return true;
}

bool PeerRpcServer::is_connected(uint64_t conn_id) const {
    return transport_ && transport_->is_connected(conn_id);
}

void PeerRpcServer::stop() {
    running_.store(false);

    // 优雅退出：关闭连接前，先对每个活跃连接触发 disconnect_handler，
    // 确保本端 pending RPC 被立即 fail（而非依赖 close_all 的 DISCONNECT
    // 事件被即将退出的 server_loop 处理——那不可靠，因为 running_=false
    // 后 loop 可能不再处理事件）。
    // 对端的 pending 释放由对端自己的 transport 检测 FIN 后触发，不依赖这里。
    if (disconnect_handler_) {
        std::vector<uint64_t> active_conns;
        {
            std::lock_guard<std::mutex> lk(buf_mutex_);
            active_conns.reserve(recv_bufs_.size());
            for (const auto& [conn_id, _] : recv_bufs_) {
                active_conns.push_back(conn_id);
            }
        }
        // 跳过已通过 BYE 正常关闭的 conn（它们不是错误断连）。
        std::lock_guard<std::mutex> bye_lk(bye_mutex_);
        for (uint64_t conn_id : active_conns) {
            if (bye_closed_conns_.count(conn_id) > 0) continue;
            disconnect_handler_(conn_id);
        }
    }

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
    {
        std::lock_guard<std::mutex> lk(bye_mutex_);
        bye_closed_conns_.clear();
        bye_ack_conns_.clear();
        bye_pending_conns_.clear();
    }
    bye_cv_.notify_all();
    transport_.reset();
    request_handler_ = nullptr;
    response_handler_ = nullptr;
    disconnect_handler_ = nullptr;
    DBG("PeerRpcServer stopped");
}

}  // namespace fly
