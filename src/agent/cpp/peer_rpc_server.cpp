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
                        // 循环切帧（一次可能收多个帧）。PeerRpc 专用直拼帧
                        // 解析（与 send_request/send_response 布局配对）：
                        // 字段直读 + payload 单次构造（原 bitsery 链路
                        // substr + FLY_DECODE take + bitsery 输出 3 次拷贝）。
                        auto& buf = it->second;
                        while (true) {
                            // 帧完整性检查：8B header + 1B type + 字段 + payload
                            if (buf.size() < 9) break;  // 不足 header，等更多数据
                            uint64_t total_len = MessageProtocol::get_total_size(buf);
                            if (total_len < 1 || buf.size() < 8 + total_len) break;  // 帧不完整
                            uint8_t raw_type = static_cast<uint8_t>(buf[8]);
                            const bool is_req =
                                raw_type == static_cast<uint8_t>(MessageType::PEER_RPC_REQUEST);
                            const bool is_resp =
                                raw_type == static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE);
                            if (!is_req && !is_resp) {
                                buf.clear();  // 未知类型，清缓冲防积压
                                break;
                            }
                            // 固定域长度：REQUEST 9+8(rpc_id)+8(src)，RESPONSE 9+8+1(status)
                            const size_t fixed = is_req ? 25 : 18;
                            if (buf.size() < fixed) break;  // 固定域未到齐，等更多数据
                            const size_t payload_len = 8 + total_len - fixed;
                            uint64_t rpc_id = read_be64(buf.data() + 9);
                            uint64_t src = is_req ? read_be64(buf.data() + 17) : 0;
                            uint8_t status = is_req ? 0 : static_cast<uint8_t>(buf[17]);
                            CMString payload(buf.data() + fixed, payload_len);
                            buf.erase(0, fixed + payload_len);
                            decoded_msgs.push_back({is_req, rpc_id, src, status,
                                                    std::move(payload)});
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
    // PeerRpc 专用直拼帧（两端同仓库同步，内部闭合）。payload 是裸 bytes，
    // 不再经 bitsery（原链路 msg.payload_ 赋值 + FLY_ENCODE 临时缓冲 +
    // frame 复制 = payload 3 次中间拷贝，大 payload 线性放大）——单 buffer
    // 组装，payload 仅 memcpy 一次。布局：
    //   [8B frame header][1B type=REQUEST][8B rpc_id BE][8B src BE][payload]
    CMString frame;
    frame.resize(9 + 16 + payload.size());
    write_be64(frame.data(), make_frame_header(1 + 16 + payload.size()));
    frame[8] = static_cast<char>(static_cast<uint8_t>(MessageType::PEER_RPC_REQUEST));
    write_be64(frame.data() + 9, rpc_id);
    write_be64(frame.data() + 17, src_worker_id);
    if (!payload.empty()) {
        std::memcpy(frame.data() + 25, payload.data(), payload.size());
    }
    ssize_t result = transport_->send(conn_id, frame);
    return result > 0;
}

bool PeerRpcServer::send_response(uint64_t conn_id, uint64_t rpc_id,
                                   uint8_t status, const CMString& payload) {
    if (!transport_) return false;
    // 专用直拼帧，同 send_request：
    //   [8B frame header][1B type=RESPONSE][8B rpc_id BE][1B status][payload]
    CMString frame;
    frame.resize(9 + 9 + payload.size());
    write_be64(frame.data(), make_frame_header(1 + 9 + payload.size()));
    frame[8] = static_cast<char>(static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE));
    write_be64(frame.data() + 9, rpc_id);
    frame[17] = static_cast<char>(status);
    if (!payload.empty()) {
        std::memcpy(frame.data() + 18, payload.data(), payload.size());
    }
    ssize_t result = transport_->send(conn_id, frame);
    return result > 0;
}

bool PeerRpcServer::notify_failure(uint64_t conn_id, const CMString& reason) {
    // notify_failure = NOTIFY_FAILURE 的 response（无需对应 request，rpc_id=0）
    return send_response(conn_id, 0,
                          static_cast<uint8_t>(PeerRpcWireStatus::NOTIFY_FAILURE), reason);
}

bool PeerRpcServer::send_not_ready(uint64_t conn_id, uint64_t rpc_id,
                                   const CMString& reason) {
    // 未就绪（可恢复）：与 RESPOND_FAILURE（真失败）在协议层区分；
    // 精确匹配 rpc_id，payload 带诊断消息。
    return send_response(conn_id, rpc_id,
                         static_cast<uint8_t>(PeerRpcWireStatus::NOT_READY),
                         reason);
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
        // 同时就地标记 bye_closed：服务端 ACK 后立即 close，随后的 DISCONNECT
        // 事件由本线程（server_loop）处理——若标记留给 send_bye 调用方线程在
        // 唤醒后补做，存在跨线程 TOCTOU（DISCONNECT 先查 bye_closed_conns_，
        // 标记尚未落位 → 误触发 disconnect_handler）。在 ACK 到达处标记后，
        // DATA(ACK) 与 DISCONNECT 的处理同线程天然有序（transport 保证
        // 数据+FIN 同时到达时先 DATA 后 DISCONNECT）。
        {
            std::lock_guard<std::mutex> lk(bye_mutex_);
            bye_ack_conns_.insert(conn_id);
            bye_closed_conns_.insert(conn_id);
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
    lk.unlock();
#ifdef FLY_ENABLE_TEST_HOOKS
    if (bye_wake_hook_for_testing_) {
        bye_wake_hook_for_testing_(conn_id, got_ack);
    }
#endif
    {
        std::lock_guard<std::mutex> relk(bye_mutex_);
        bye_closed_conns_.insert(conn_id);  // 正常关闭标记（ACK 路径已由 handle_bye 同线程先行标记，此处幂等）
    }

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
