#include <agent/cpp/peer_rpc_server.h>
#include <storage/cpp/pipeline.h>
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
                    // 流式零容忍（坏帧/对账失配）：锁内只置位，锁外统一
                    // close——close_connection 内部 lock 同一把 buf_mutex_，
                    // 持锁调用会自死锁（std::mutex 不可重入）。
                    bool stream_fatal = false;
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
                            if (total_len < 1) {
                                buf.clear();  // 坏头：清缓冲防积压
                                break;
                            }
                            if (buf.size() < 8 + total_len) break;  // 帧不完整
                            uint8_t raw_type = static_cast<uint8_t>(buf[8]);
                            const bool is_req =
                                raw_type == static_cast<uint8_t>(MessageType::PEER_RPC_REQUEST);
                            const bool is_resp =
                                raw_type == static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE);
                            if (!is_req && !is_resp) {
                                // ── 流式帧（连接独占：START→DATA×N→END）──
                                if (raw_type == static_cast<uint8_t>(MessageType::PEER_STREAM_START)) {
                                    PeerStreamStartMessage m;
                                    if (!MessageProtocol::decode(buf, m)) {
                                        ERR("[PEER-STREAM] corrupt START, closing conn");
                                        buf.clear();
                                        stream_fatal = true;
                                        break;
                                    }
                                    auto& s = streams_[event.conn_id_];
                                    s.rpc_id = m.rpc_id_;
                                    s.direction = m.direction_;
                                    s.compression_type = m.compression_type_;
                                    s.consumed = 0;
                                    s.plain_total = 0;
                                    s.chunks = 0;
                                    s.verify_failed = false;
                                    s.have_hdr = false;
                                    s.block_acc.clear();
                                    s.plain.clear();
                                    // 解压器按流声明构造（NONE 为空 = raw 直通）。
                                    s.decompressor =
                                        m.compression_type_ == static_cast<uint8_t>(CompressionType::NONE)
                                            ? nullptr
                                            : CompressorFactory::create(
                                                  static_cast<CompressionType>(m.compression_type_));
                                    s.active = true;
                                    continue;
                                }
                                if (raw_type == static_cast<uint8_t>(MessageType::PEER_STREAM_END)) {
                                    PeerStreamEndMessage m;
                                    if (!MessageProtocol::decode(buf, m)) {
                                        ERR("[PEER-STREAM] corrupt END, closing conn");
                                        buf.clear();
                                        stream_fatal = true;
                                        break;
                                    }
                                    auto sit = streams_.find(event.conn_id_);
                                    if (sit == streams_.end() || !sit->second.active) {
                                        ERR("[PEER-STREAM] END without active stream, closing conn");
                                        buf.clear();
                                        stream_fatal = true;
                                        break;
                                    }
                                    // 明文已随接收流水解压完毕——此处仅对账。
                                    // 失配 = 零容忍 close（不交付坏 payload）。
                                    if (!sit->second.verify_failed &&
                                        verify_stream_end(sit->second, m.total_uncompressed_,
                                                          m.chunk_count_, m.consumed_)) {
                                        decoded_msgs.push_back(
                                            {sit->second.direction == 0, m.rpc_id_, 0,
                                             static_cast<uint8_t>(PeerRpcWireStatus::OK),
                                             std::move(sit->second.plain)});
                                    } else {
                                        ERR("[PEER-STREAM] stream verify failed, closing conn");
                                        // 零容忍：坏流不交付，断连唤醒调用方。
                                        stream_fatal = true;
                                        break;
                                    }
                                }
                                if (raw_type == static_cast<uint8_t>(MessageType::DATA_CHUNK)) {
                                    auto sit = streams_.find(event.conn_id_);
                                    if (sit == streams_.end() || !sit->second.active) {
                                        // 无流上下文：协议错位——丢弃整帧防积压。
                                        WARN("[PEER-STREAM] DATA without active stream ({}B), discarding",
                                             8 + total_len);
                                        buf.erase(0, 8 + total_len);
                                        continue;
                                    }
                                    // 帧头 29B（8B frame + 1B type + 4B small_len + 16B
                                    // small fields）；payload = 块流字节。
                                    // raw = total_len - (1 + 4 + 16)——total_len 不含
                                    // 外层 8B frame header。
                                    const uint64_t raw_len =
                                        ChunkFrameProtocol::raw_len_from_total(total_len);
                                    if (buf.size() < 8 + total_len) break;  // 帧不完整，等更多数据
                                    if (raw_len == 0) {
                                        ERR("[PEER-STREAM] DATA_CHUNK zero raw_len");
                                        buf.clear();
                                        break;
                                    }
                                    // 边收边解：块流字节即到即喂跨帧块解析器
                                    // （逐块 CRC 验证 + 解压与接收流水重叠）。
                                    if (!feed_stream_bytes(sit->second, buf.data() + 29,
                                                           raw_len)) {
                                        ERR("[PEER-STREAM] stream data error, closing conn");
                                        buf.clear();
                                        stream_fatal = true;
                                        break;
                                    }
                                    sit->second.consumed += raw_len;
                                    buf.erase(0, 8 + total_len);
                                    continue;
                                }
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
                    if (stream_fatal) {
                        streams_.erase(event.conn_id_);
                        close_connection(event.conn_id_);  // 锁外：DISCONNECT 兜底唤醒调用方
                        break;
                    }
                    // 锁外调 handler（回调可能耗时，不应持锁）。
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
                        streams_.erase(event.conn_id_);
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

bool PeerRpcServer::send_stream_start(uint64_t conn_id, uint64_t rpc_id,
                                      uint8_t direction, uint8_t compression_type) {
    if (!transport_) return false;
    PeerStreamStartMessage m;
    m.rpc_id_ = rpc_id;
    m.direction_ = direction;
    m.compression_type_ = compression_type;
    CMString frame = MessageProtocol::encode(m);
    return transport_->send(conn_id, frame) > 0;
}

bool PeerRpcServer::send_stream_data(uint64_t conn_id, const char* data, size_t n) {
    if (!transport_) return false;
    CMString hdr = ChunkFrameProtocol::encode_header(0, 0, n);
    if (transport_->send(conn_id, hdr) <= 0) return false;
    if (n == 0) return true;
    return transport_->send(conn_id, CMString(data, n)) > 0;
}

bool PeerRpcServer::send_stream_end(uint64_t conn_id, uint64_t rpc_id,
                                    uint64_t total_uncompressed, uint32_t chunk_count,
                                    uint64_t consumed) {
    if (!transport_) return false;
    PeerStreamEndMessage m;
    m.rpc_id_ = rpc_id;
    m.total_uncompressed_ = total_uncompressed;
    m.chunk_count_ = chunk_count;
    m.consumed_ = consumed;
    CMString frame = MessageProtocol::encode(m);
    return transport_->send(conn_id, frame) > 0;
}

bool PeerRpcServer::send_stream_payload(uint64_t conn_id, uint64_t rpc_id,
                                        uint8_t direction, const CMString& payload,
                                        CompressionType comp, int level,
                                        uint64_t& total_out, uint32_t& chunks_out) {
    if (!transport_) return false;
    // 整 payload 装配为流（无 writer 分步 API 时的便捷封装）：一次 write +
    // finish，压缩管线逐块产出，4MB 切帧发送。
    PeerStreamWriter w(this, conn_id, rpc_id, direction, comp, level);
    if (!w.ok()) return false;
    w.write(payload.data(), payload.size());
    if (!w.finish()) return false;
    total_out = w.total_uncompressed();
    chunks_out = w.chunk_count();
    return true;
}

// ── PeerStreamWriter：明文 → 压缩管线 → 4MB DATA_CHUNK 切帧 ──

PeerStreamWriter::PeerStreamWriter(PeerRpcServer* srv, uint64_t conn_id,
                                   uint64_t rpc_id, uint8_t direction,
                                   CompressionType comp, int level)
    : srv_(srv), conn_id_(conn_id), rpc_id_(rpc_id) {
    if (!srv_ || conn_id_ == 0) {
        ERR("[PEER-STREAM-W] invalid construction");
        return;
    }
    started_ = srv_->send_stream_start(conn_id_, rpc_id_, direction,
                                       static_cast<uint8_t>(comp));
    if (!started_) {
        ERR("[PEER-STREAM-W] send START failed conn={}", conn_id_);
        return;
    }
    thread_ = std::thread([this, comp, level] { compress_loop(comp, level); });
}

PeerStreamWriter::~PeerStreamWriter() {
    if (thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(qm_);
            producer_closed_ = true;
        }
        q_data_cv_.notify_all();
        thread_.join();
    }
}

void PeerStreamWriter::enqueue(const char* data, size_t n) {
    // 有界入队：满则等空间（背压传播到序列化调用方）；发送线程停止时
    // 丢弃剩余（流已死）。
    while (n > 0) {
        std::unique_lock<std::mutex> lk(qm_);
        if (consumer_stopped_) return;
        q_space_cv_.wait(lk, [&] {
            return consumer_stopped_ || producer_closed_ ||
                   q_bytes_ < kQueueBytes;
        });
        if (consumer_stopped_) return;
        const size_t take = std::min(n, kQueueBytes - q_bytes_);
        plain_q_.emplace_back(data, data + take);
        data += take;
        n -= take;
        q_bytes_ += take;
        q_data_cv_.notify_one();
    }
}

void PeerStreamWriter::write(const char* data, size_t n) {
    if (!started_ || producer_closed_) return;
    const auto t0 = std::chrono::steady_clock::now();
    enqueue(data, n);
    write_wait_ns_ += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t0).count());
}

bool PeerStreamWriter::finish() {
    if (finished_) return ok_;
    finished_ = true;
    if (!started_) return false;
    {
        std::lock_guard<std::mutex> lk(qm_);
        producer_closed_ = true;
    }
    q_data_cv_.notify_all();
    if (!joined_) {
        thread_.join();
        joined_ = true;
    }
    return ok_;
}

// 压缩线程：明文队列 → 管线（压缩 → CRC → 块记录）→ 块记录直发。
// 阶段计时：compress_ns = 出队+压缩+组帧；send_ns = socket 发送——
// 两者之和 vs 总墙钟的比值即并行度证据（write_wait_ns = 生产端背压）。
void PeerStreamWriter::compress_loop(CompressionType comp, int level) {
    // 直发端点：块记录 = [16B 块头][payload]。头部凑齐后 payload 原地
    // 组 DATA_CHUNK 帧直发连接发送队列——不经累积缓冲（frame_off_ 仍按
    // 已发压缩字节累计，与接收端 consumed 对账语义不变）。
    auto emit = [this](const char* d, size_t n) {
        if (blk_hdr_len_ < 16) {
            const size_t take = std::min(n, static_cast<size_t>(16 - blk_hdr_len_));
            std::memcpy(blk_hdr_ + blk_hdr_len_, d, take);
            blk_hdr_len_ += static_cast<uint32_t>(take);
            d += take;
            n -= take;
            if (n == 0) return;   // 头未齐或恰齐（payload 由后续 emit 送出）
        }
        send_block_frame(d, n);
    };
    std::vector<std::unique_ptr<fly::WriteStage>> stages;
    if (comp != CompressionType::NONE) {
        stages.push_back(std::make_unique<fly::CompressStage>(
            CompressorFactory::create(comp, level)));
    }
    stages.push_back(std::make_unique<fly::CrcStage>());
    // 末端：块记录直发（漏掉此 Stage 则块头不产出、payload 不出管线）。
    stages.push_back(std::make_unique<fly::BlockHeaderStage>(emit));
    fly::WritePipeline pipeline(std::move(stages), kFrameBytes, emit);

    while (true) {
        std::vector<char> chunk;
        {
            std::unique_lock<std::mutex> lk(qm_);
            q_data_cv_.wait(lk, [&] {
                return !plain_q_.empty() || producer_closed_;
            });
            if (plain_q_.empty()) break;  // closed + 排空
            chunk = std::move(plain_q_.front());
            plain_q_.pop_front();
            q_bytes_ -= chunk.size();
        }
        q_space_cv_.notify_one();
        const auto t0 = std::chrono::steady_clock::now();
        pipeline.write(chunk.data(), chunk.size());
        compress_ns_ += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0).count());
    }
    pipeline.finish();
    flush_pending_block();
    total_uncompressed_ = pipeline.total_uncompressed();
    chunk_count_ = pipeline.chunk_count();

    // END 对账帧（压缩线程尾部发出——所有数据帧先于它到达）。
    if (send_ok_) {
        ok_ = srv_->send_stream_end(conn_id_, rpc_id_, total_uncompressed_,
                                    chunk_count_, frame_off_);
        if (!ok_) ERR("[PEER-STREAM-W] send END failed conn={}", conn_id_);
    }
}

void PeerStreamWriter::send_block_frame(const char* payload, size_t n) {
    CMString hdr = ChunkFrameProtocol::encode_header(frame_off_, 0, 16 + n);
    hdr.append(blk_hdr_, 16);
    blk_hdr_len_ = 0;
    if (!send_ok_) return;
    const auto t0 = std::chrono::steady_clock::now();
    const bool sent = srv_->transport_send_raw(conn_id_, hdr) &&
                      (n == 0 || srv_->transport_send_raw(conn_id_, payload, n));
    send_ns_ += static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - t0).count());
    frame_off_ += 16 + n;
    if (!sent) {
        send_ok_ = false;
        ERR("[PEER-STREAM-W] send frame failed conn={}", conn_id_);
    }
}

void PeerStreamWriter::flush_pending_block() {
    if (blk_hdr_len_ == 0) return;
    if (blk_hdr_len_ == 16) {
        send_block_frame(nullptr, 0);   // header-only 记录（空块防御）
        return;
    }
    ERR("[PEER-STREAM-W] partial block header, aborting stream conn={}",
        conn_id_);
    send_ok_ = false;
    blk_hdr_len_ = 0;
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

bool PeerRpcServer::feed_stream_bytes(StreamRx& s, const char* data, size_t n) {
    while (n > 0) {
        if (!s.have_hdr) {
            const size_t need = 16 - s.block_acc.size();
            const size_t take = std::min(n, need);
            s.block_acc.append(data, take);
            data += take;
            n -= take;
            if (s.block_acc.size() < 16) return true;  // 头未齐，等更多
            std::memcpy(&s.p_unc, s.block_acc.data(), 4);
            std::memcpy(&s.p_comp, s.block_acc.data() + 4, 4);
            std::memcpy(&s.p_crc, s.block_acc.data() + 8, 8);
            s.have_hdr = true;
        }
        // 数据阶段：凑齐 p_comp 字节压缩数据。
        const size_t got_data = s.block_acc.size() - 16;
        const size_t need_data = s.p_comp - got_data;
        const size_t take = std::min(n, need_data);
        s.block_acc.append(data, take);
        data += take;
        n -= take;
        if (s.block_acc.size() - 16 < s.p_comp) return true;  // 数据未齐

        // 完整块：CRC 验证（压缩态，写入时刻锚点）+ 解压 → 明文累积。
        const char* payload = s.block_acc.data() + 16;
        if (fly::data_checksum(payload, s.p_comp) != s.p_crc) {
            ERR("[PEER-STREAM] block CRC mismatch");
            s.verify_failed = true;
            return false;
        }
        if (s.decompressor && s.p_comp != s.p_unc) {
            s.dec_buf.resize(s.p_unc);
            const int32_t written = s.decompressor->decompress_to(
                {payload, s.p_comp}, s.dec_buf.data(), s.p_unc);
            if (written < 0 || static_cast<uint32_t>(written) != s.p_unc) {
                ERR("[PEER-STREAM] decompress failed");
                s.verify_failed = true;
                return false;
            }
            s.plain.append(s.dec_buf.data(), s.p_unc);
        } else {
            s.plain.append(payload, s.p_comp);  // raw 直通（comp == unc）
        }
        s.plain_total += s.p_unc;
        s.chunks++;
        s.block_acc.clear();
        s.have_hdr = false;
    }
    return true;
}

bool PeerRpcServer::verify_stream_end(const StreamRx& s, uint64_t total,
                                      uint32_t chunks, uint64_t consumed) {
    // 三重对账：明文总量 / 块数 / 消费压缩字节数。
    return !s.verify_failed && s.plain_total == total &&
           s.chunks == chunks && s.consumed == consumed;
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
