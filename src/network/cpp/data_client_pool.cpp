#include <network/cpp/data_client_pool.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_quality_monitor.h>
#include <common/cpp/data_checksum.h>
#include <serialization/cpp/object_header.h>
#include <log/cpp/logger.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <new>
#include <sys/socket.h>

namespace fly {

DataClientPool::DataClientPool(CMSharedPtr<Transport> transport, int64_t pool_size)
    : transport_(std::move(transport))
    , pool_size_(pool_size > 0 ? pool_size : 2)
    , max_fd_count_(2 * (pool_size > 0 ? pool_size : 2)) {}

DataClientPool::DataClientPool(int64_t pool_size)
    : DataClientPool(create_tcp_transport(), pool_size) {}

DataClientPool::~DataClientPool() {
    stop();
}

CMString DataClientPool::make_peer_key(const CMString& host, int port) {
    return host + ":" + std::to_string(port);
}

bool DataClientPool::probe_fd_health(int fd) const {
    // 廉价预检：getsockopt(SO_ERROR) 捕获连接级错误（RST/重置）。注意 TOCTOU——
    // 通过后 send/recv 前仍可能断，故仅作预检，权威仍是 send/recv 失败即关。
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) return false;
    return err == 0;
}

int DataClientPool::try_acquire_idle_locked(const CMString& key) {
    auto it = buckets_.find(key);
    if (it == buckets_.end()) return -1;
    for (auto& e : it->second) {
        if (!e.in_use) {
            e.in_use = true;
            e.last_used = std::chrono::steady_clock::now();
            return e.fd;
        }
    }
    return -1;
}

bool DataClientPool::evict_one_idle_locked() {
    // 反倾斜：选 idle 数最多的 peer（并列取其中最老 last_used），淘汰其最老 idle fd。
    // 防止单 hot peer 独占连接配额，保留多样性以提高未来命中率。
    const CMString* victim_peer = nullptr;
    size_t max_idle = 0;
    std::chrono::steady_clock::time_point victim_oldest =
        std::chrono::steady_clock::time_point::max();
    for (auto& [pk, bucket] : buckets_) {
        size_t idle = 0;
        auto oldest = std::chrono::steady_clock::time_point::max();
        for (auto& e : bucket) {
            if (!e.in_use) {
                ++idle;
                if (e.last_used < oldest) oldest = e.last_used;
            }
        }
        if (idle == 0) continue;
        if (idle > max_idle || (idle == max_idle && oldest < victim_oldest)) {
            max_idle = idle;
            victim_peer = &pk;
            victim_oldest = oldest;
        }
    }
    if (!victim_peer) return false;
    auto& bucket = buckets_[*victim_peer];
    auto vit = std::find_if(bucket.begin(), bucket.end(), [&](const FdEntry& e) {
        return !e.in_use && e.last_used == victim_oldest;
    });
    if (vit == bucket.end()) return false;
    transport_->close(vit->fd);
    fd_to_peer_.erase(vit->fd);
    bucket.erase(vit);
    --total_fd_count_;
    return true;
}

void DataClientPool::remove_fd_locked(int fd) {
    auto pit = fd_to_peer_.find(fd);
    if (pit == fd_to_peer_.end()) return;
    auto& bucket = buckets_[pit->second];
    bucket.erase(std::remove_if(bucket.begin(), bucket.end(),
                                [fd](const FdEntry& e) { return e.fd == fd; }),
                 bucket.end());
    fd_to_peer_.erase(pit);
    --total_fd_count_;
}

void DataClientPool::reap_expired_locked() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = buckets_.begin(); it != buckets_.end();) {
        auto& bucket = it->second;
        for (auto bit = bucket.begin(); bit != bucket.end();) {
            if (!bit->in_use &&
                now - bit->last_used > std::chrono::seconds(kIdleTtlSec)) {
                transport_->close(bit->fd);
                fd_to_peer_.erase(bit->fd);
                bit = bucket.erase(bit);
                --total_fd_count_;
            } else {
                ++bit;
            }
        }
        if (bucket.empty()) {
            it = buckets_.erase(it);
        } else {
            ++it;
        }
    }
}

int DataClientPool::borrow_fd(const CMString& host, int port) {
    const CMString key = make_peer_key(host, port);
    while (true) {
        int fd = -1;
        bool need_new = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            reap_expired_locked();
            fd = try_acquire_idle_locked(key);
            if (fd < 0) {
                if (total_fd_count_ >= max_fd_count_) {
                    // 容量满：反倾斜淘汰一个 idle 腾配额（idle ≥ pool_size ≥ 1，必能淘汰）
                    if (!evict_one_idle_locked()) return -1;
                }
                ++total_fd_count_;  // 预留配额，避免并发 connect 导致超限
                need_new = true;
            }
        }
        if (need_new) {
            // connect 是阻塞系统调用，在锁外执行避免阻塞其他 borrow/release
            int nfd = transport_->create_connection(host, port);
            if (nfd < 0) {
                std::lock_guard<std::mutex> lk(mutex_);
                --total_fd_count_;  // 回退预留配额
                return -1;
            }
            transport_->set_recv_timeout(nfd, 30000);
            transport_->set_send_timeout(nfd, 30000);
            transport_->set_nodelay(nfd);
            std::lock_guard<std::mutex> lk(mutex_);
            buckets_[key].push_back(
                FdEntry{nfd, std::chrono::steady_clock::now(), true});
            fd_to_peer_[nfd] = key;
            return nfd;
        }
        // 复用 idle fd：锁外做 SO_ERROR 预检（已 in_use 占住，不会被他人复用）
        if (probe_fd_health(fd)) return fd;
        // 预检失败（半开）：关闭回收，循环重试
        transport_->close(fd);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            remove_fd_locked(fd);
        }
    }
}

void DataClientPool::release_fd(int fd, bool healthy) {
    if (!healthy) {
        transport_->close(fd);
        std::lock_guard<std::mutex> lk(mutex_);
        remove_fd_locked(fd);
        return;
    }
    std::lock_guard<std::mutex> lk(mutex_);
    auto pit = fd_to_peer_.find(fd);
    if (pit == fd_to_peer_.end()) return;  // 已不在池（stop 期间被清理），忽略
    auto& bucket = buckets_[pit->second];
    for (auto& e : bucket) {
        if (e.fd == fd) {
            e.in_use = false;
            e.last_used = std::chrono::steady_clock::now();
            break;
        }
    }
}

DataClientPool::RawExchange DataClientPool::request_raw_exchange(
    const CMString& host, int port, const CMString& object_name, int timeout_ms) {
    RawExchange out;
    if (stopped_.load()) {
        out.error = "Pool stopped";
        out.rerr = ReadError::SHUTDOWN;
        return out;
    }

    {
        std::unique_lock<std::mutex> lk(mutex_);
        slot_cv_.wait(lk, [&] {
            return stopped_.load() || active_count_.load() < static_cast<int>(pool_size_);
        });
        if (stopped_.load()) {
            out.error = "Pool stopped";
            out.rerr = ReadError::SHUTDOWN;
            return out;
        }
        active_count_.fetch_add(1);
    }

    int fd = borrow_fd(host, port);
    if (fd < 0) {
        active_count_.fetch_sub(1);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            slot_cv_.notify_one();
        }
        out.error = "Failed to connect to " + host + ":" + std::to_string(port);
        out.rerr = ReadError::NETWORK;
        return out;
    }

    DataRequestMessage req;
    req.object_name_ = object_name;
    CMString encoded_req = MessageProtocol::encode(req);
    const char* send_ptr = encoded_req.data();
    size_t send_remaining = encoded_req.size();
    while (send_remaining > 0) {
        ssize_t n = transport_->send(fd, send_ptr, send_remaining);
        if (n < 0) {
            release_fd(fd, false);
            active_count_.fetch_sub(1);
            {
                std::lock_guard<std::mutex> lk(mutex_);
                slot_cv_.notify_one();
            }
            out.error = "Connection lost sending request for " + object_name;
            out.rerr = ReadError::NETWORK;
            return out;
        }
        send_ptr += n;
        send_remaining -= static_cast<size_t>(n);
    }

    // 读 META / 快路径整帧响应的头部。
    char frame_header[9];
    if (!recv_exact(transport_.get(), fd, frame_header, 9)) {
        release_fd(fd, false);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.error = "Connection lost for " + object_name;
        out.rerr = ReadError::NETWORK;
        return out;
    }
    uint64_t total_len = 0;
    if (!parse_frame_header(frame_header, total_len)) {
        release_fd(fd, false);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.error = "Invalid response for " + object_name;
        out.rerr = ReadError::CHECKSUM;
        return out;
    }
    if (total_len < 6) {
        release_fd(fd, false);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.error = "Invalid response for " + object_name;
        out.rerr = ReadError::NETWORK;
        return out;
    }

    char sub_header[5];
    if (!recv_exact(transport_.get(), fd, sub_header, 5)) {
        release_fd(fd, false);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.error = "Connection lost for " + object_name;
        out.rerr = ReadError::NETWORK;
        return out;
    }
    uint32_t small_fields_len = 0;
    bool has_raw = false;
    DataResponseProtocol::parse_sub_header(sub_header, small_fields_len, has_raw);

    CMString small_payload(small_fields_len, '\0');
    if (small_fields_len > 0 &&
        !recv_exact(transport_.get(), fd, small_payload.data(), small_fields_len)) {
        release_fd(fd, false);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.error = "Connection lost for " + object_name;
        out.rerr = ReadError::NETWORK;
        return out;
    }
    DataResponseMessage response;
    if (!DataResponseProtocol::decode_small_fields(small_payload, response)) {
        release_fd(fd, false);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.error = "Failed to decode response for " + object_name;
        out.rerr = ReadError::NETWORK;
        return out;
    }

    // 协议级失败（NOT_READY/NOT_FOUND）：fd 已完成交换可归还复用。
    if (!response.success_) {
        release_fd(fd, true);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.meta = response;
        out.rerr = (response.status_ == ResponseStatus::NOT_READY)
                       ? ReadError::DATA_NOT_READY
                   : (response.status_ == ResponseStatus::NOT_FOUND)
                       ? ReadError::OBJECT_NOT_FOUND
                       : ReadError::NETWORK;
        out.error = response.error_message_;
        return out;
    }

    if (response.chunked_) {
        // 分片流：fd 借出（后续帧由调用方的接收线程消费；release_borrowed_fd
        // 归还 fd + slot——slot 归也在那时，保持并发预算"每流占一 slot"）。
        out.success = true;
        out.meta = response;
        out.fd = fd;
        return out;
    }

    // 快路径：读完 raw + 验 wire 根 → 整缓冲返回（fd/slot 即刻归还）。
    if (has_raw) {
        uint64_t raw_len = DataResponseProtocol::raw_len_from_total(total_len, small_fields_len);
        auto buf = CMMakeShared<FlyBuffer>();
        try {
            buf->resize(raw_len);
        } catch (const std::bad_alloc&) {
            release_fd(fd, false);
            active_count_.fetch_sub(1);
            { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
            out.error = "Payload too large to buffer: " + object_name;
            out.rerr = ReadError::NETWORK;
            return out;
        }
        if (!recv_exact(transport_.get(), fd, buf->data(), raw_len)) {
            release_fd(fd, false);
            active_count_.fetch_sub(1);
            { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
            out.error = "Connection lost receiving payload for " + object_name;
            out.rerr = ReadError::NETWORK;
            return out;
        }
        if (response.payload_crc_ != 0 &&
            data_checksum(buf->data(), buf->size()) != response.payload_crc_) {
            ERR("[DCP-FATAL-DATA-CORRUPTION] wire root CRC mismatch: obj={} fd={}",
                object_name, fd);
            release_fd(fd, false);
            active_count_.fetch_sub(1);
            { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
            out.error = "Wire CRC mismatch for " + object_name;
            out.rerr = ReadError::CHECKSUM;
            return out;
        }
        release_fd(fd, true);
        active_count_.fetch_sub(1);
        { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
        out.success = true;
        out.meta = response;
        out.whole_data = buf;
        return out;
    }

    // 成功但无 raw（异常状态）：按失败处理。
    release_fd(fd, true);
    active_count_.fetch_sub(1);
    { std::lock_guard<std::mutex> lk(mutex_); slot_cv_.notify_one(); }
    out.error = "Empty response for " + object_name;
    out.rerr = ReadError::NETWORK;
    return out;
}

void DataClientPool::release_borrowed_fd(int fd, bool healthy) {
    if (fd < 0) return;
    release_fd(fd, healthy);
    active_count_.fetch_sub(1);
    {
        // 持锁 notify（lost wakeup 防御，同 release_slot）。
        std::lock_guard<std::mutex> lk(mutex_);
        slot_cv_.notify_one();
    }
}

std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString, ReadError> DataClientPool::request(
    const CMString& host,
    int port,
    const CMString& object_name,
    uint64_t requesting_worker_id,
    uint64_t request_id,
    int timeout_ms){
    if (stopped_.load()) {
        return {false, nullptr, "", "", "Pool stopped", ReadError::SHUTDOWN};
    }

    {
        std::unique_lock<std::mutex> lk(mutex_);
        slot_cv_.wait(lk, [&] {
            return stopped_.load() || active_count_.load() < static_cast<int>(pool_size_);
        });
        if (stopped_.load()) {
            return {false, nullptr, "", "", "Pool stopped", ReadError::SHUTDOWN};
        }
        active_count_.fetch_add(1);
    }

    auto release_slot = [&]() {
        active_count_.fetch_sub(1);
        // 持锁 notify：slot_cv_ 的 waiter 是无超时谓词 wait（request 入口与
        // stop），无锁 notify 存在 lost wakeup 窗口（谓词检查后、进入 wait 前
        // notify 落空 → 请求方/stop 永久挂死）。
        std::lock_guard<std::mutex> lk(mutex_);
        slot_cv_.notify_one();
    };

    int fd = borrow_fd(host, port);
    if (fd < 0) {
        release_slot();
        return {false, nullptr, "", "",
                "Failed to connect to " + host + ":" + std::to_string(port),
                ReadError::NETWORK};
    }

    // Passive RTT probe: time the round-trip. Only completed exchanges (the two
    // returns below that read a full response) record a sample; mid-recv
    // failures bail out early and skip this.
    auto rtt_start = std::chrono::steady_clock::now();

    DataRequestMessage req;
    req.object_name_ = object_name;
    req.requesting_worker_id_ = requesting_worker_id;
    req.request_id_ = request_id;
    CMString encoded_req = MessageProtocol::encode(req);

    // NOTE: this pool performs exactly ONE request per call. DATA_NOT_READY is
    // returned to the caller (ReadError::DATA_NOT_READY) so the TIER2 layer
    // owns backoff/retry policy. No internal polling loop here.
    while (true) {
        // send all
        const char* send_ptr = encoded_req.data();
        size_t send_remaining = encoded_req.size();
        while (send_remaining > 0) {
            ssize_t n = transport_->send(fd, send_ptr, send_remaining);
            if (n < 0) {
                ERR("[DCP] send failed: obj={} fd={} errno={}", object_name, fd, errno);
                release_fd(fd, false);
                release_slot();
                return {false, nullptr, "", "",
                        "Connection lost sending request for " + object_name,
                        ReadError::NETWORK};
            }
            send_ptr += n;
            send_remaining -= static_cast<size_t>(n);
        }

        // ── Two-segment response read (DATA_RESPONSE protocol) ──

        // 1. Read 9B frame header [8B header: (check<<48)|len][1B type]
        //    check 位失配 = 流失步/垃圾/损坏 → 连接作废（不重用 fd）。
        //    归 CHECKSUM 类（§5：校验类错误，驱动一次重取而非网络退避）。
        char frame_header[9];
        if (!recv_exact(transport_.get(), fd, frame_header, 9)) {
            ERR("[DCP] recv header failed: obj={} fd={} errno={}", object_name, fd, errno);
            release_fd(fd, false);
            release_slot();
            return {false, nullptr, "", "",
                    "Connection lost for " + object_name,
                    ReadError::NETWORK};
        }
        uint64_t total_len = 0;
        if (!parse_frame_header(frame_header, total_len)) {
            ERR("[DCP] frame header check failed: obj={} fd={}", object_name, fd);
            release_fd(fd, false);
            release_slot();
            return {false, nullptr, "", "",
                    "Invalid response for " + object_name,
                    ReadError::CHECKSUM};
        }
        // 下界 6 = 1(type) + 4(small_fields_len) + 1(has_raw)。上限交给
        // check 位 + resize 的 bad_alloc 捕获（巨型对象合法，256MB 假上限已删）。
        if (total_len < 6) {
            ERR("[DCP] invalid total_len={}: obj={} fd={}", total_len, object_name, fd);
            release_fd(fd, false);
            release_slot();
            return {false, nullptr, "", "",
                    "Invalid response for " + object_name,
                    ReadError::NETWORK};
        }

        // 2. Read 5B sub-header [4B small_fields_len][1B has_raw]
        char sub_header[5];
        if (!recv_exact(transport_.get(), fd, sub_header, 5)) {
            release_fd(fd, false);
            release_slot();
            return {false, nullptr, "", "",
                    "Connection lost for " + object_name,
                    ReadError::NETWORK};
        }
        uint32_t small_fields_len = 0;
        bool has_raw = false;
        DataResponseProtocol::parse_sub_header(sub_header, small_fields_len, has_raw);

        // 3. Read small_fields_len bytes → FLY_DECODE → msg
        CMString small_payload(small_fields_len, '\0');
        if (small_fields_len > 0) {
            if (!recv_exact(transport_.get(), fd, small_payload.data(), small_fields_len)) {
                release_fd(fd, false);
                release_slot();
                return {false, nullptr, "", "",
                        "Connection lost for " + object_name,
                        ReadError::NETWORK};
            }
        }
        DataResponseMessage response;
        if (!DataResponseProtocol::decode_small_fields(small_payload, response)) {
            ERR("[DCP] decode failed: obj={} fd={}", object_name, fd);
            release_fd(fd, false);
            release_slot();
            return {false, nullptr, "", "",
                    "Failed to decode response for " + object_name,
                    ReadError::NETWORK};
        }

        // ── L2 分片接收模式（§4.5）：META（chunked_=true，无 raw）先行 ──
        if (response.chunked_) {
            auto result = receive_chunked(fd, object_name, response);
            bool ok = std::get<0>(result);
            if (ok) {
                double rtt_ms = std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - rtt_start)
                                    .count();
                NetQualityMonitor::instance().update_rtt(host, rtt_ms);
                release_fd(fd, true);
            } else {
                release_fd(fd, false);
            }
            release_slot();
            return result;
        }

        // 4. If has_raw: read raw payload directly into FlyBuffer
        FlyBufferPtr data_buf;
        if (has_raw) {
            uint64_t raw_len = DataResponseProtocol::raw_len_from_total(total_len, small_fields_len);
            data_buf = CMMakeShared<FlyBuffer>();
            try {
                data_buf->resize(raw_len);
            } catch (const std::bad_alloc&) {
                // check 位已拒绝垃圾头；能走到这里的是真巨型对象 vs 内存不足，
                // 或罕见漏网垃圾——统一按 NETWORK 上抛（不崩溃进程）。
                ERR("[DCP] payload alloc failed: obj={} raw_len={}", object_name, raw_len);
                release_fd(fd, false);
                release_slot();
                return {false, nullptr, "", "",
                        "Payload too large to buffer: " + object_name,
                        ReadError::NETWORK};
            }
            if (!recv_exact(transport_.get(), fd, data_buf->data(), raw_len)) {
                ERR("[DCP] recv raw failed: obj={} fd={} errno={}", object_name, fd, errno);
                release_fd(fd, false);
                release_slot();
                return {false, nullptr, "", "",
                        "Connection lost receiving payload for " + object_name,
                        ReadError::NETWORK};
            }
            // wire 根摘要（§4.5）：server 锚点 vs 本地重算。失配 = 传输跳
            //（内存/网络/代码缺陷）→ CHECKSUM（连接作废 + 一次重取语义）。
            if (response.payload_crc_ != 0 &&
                data_checksum(data_buf->data(), data_buf->size()) != response.payload_crc_) {
                ERR("[DCP-FATAL-DATA-CORRUPTION] wire root CRC mismatch: obj={} fd={} "
                    "expected={:016x} actual={:016x}",
                    object_name, fd, response.payload_crc_,
                    data_checksum(data_buf->data(), data_buf->size()));
                release_fd(fd, false);
                release_slot();
                return {false, nullptr, "", "",
                        "Wire CRC mismatch for " + object_name,
                        ReadError::CHECKSUM};
            }
        }

        if (response.success_) {
            DBG("[DCP] success: obj={} fd={} data_size={}", object_name, fd,
                data_buf ? data_buf->size() : 0);
            double rtt_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - rtt_start)
                                .count();
            NetQualityMonitor::instance().update_rtt(host, rtt_ms);
            release_fd(fd, true);
            release_slot();
            return {true, data_buf, response.py_name_,
                    response.write_context_hash_, "", ReadError::NONE};
        }

        // Failure: classify and return. DATA_NOT_READY and OBJECT_NOT_FOUND are
        // protocol-level; everything else is NETWORK. The caller (TIER2) decides
        // whether/how to retry — the pool does not poll. A full response was
        // still exchanged, so this is a valid RTT sample.
        double rtt_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - rtt_start)
                            .count();
        NetQualityMonitor::instance().update_rtt(host, rtt_ms);
        release_fd(fd, true);
        release_slot();
        ReadError rerr = (response.status_ == ResponseStatus::NOT_READY)
                             ? ReadError::DATA_NOT_READY
                         : (response.status_ == ResponseStatus::NOT_FOUND)
                             ? ReadError::OBJECT_NOT_FOUND
                             : ReadError::NETWORK;
        return {false, nullptr, "", "", response.error_message_, rerr};
    }
}

std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString, ReadError>
DataClientPool::receive_chunked(int fd, const CMString& object_name,
                                const DataResponseMessage& meta) {
    uint64_t total = meta.total_compressed_len_;
    // 切片尺寸来自 META（发送端实现细节）：client 按 seq*frame 定位填充。
    // frame > total 合法（单片场景：对象介于阈值与切片尺寸之间，chunk_count=1）。
    if (total == 0 || meta.chunk_frame_bytes_ == 0) {
        ERR("[DCP-CHUNK] invalid META: obj={} total={} frame={}",
            object_name, total, meta.chunk_frame_bytes_);
        return {false, nullptr, "", "", "Invalid chunked META for " + object_name,
                ReadError::NETWORK};
    }
    const uint64_t frame = meta.chunk_frame_bytes_;
    uint64_t chunk_count = (total + frame - 1) / frame;

    FlyBufferPtr buf = CMMakeShared<FlyBuffer>();
    try {
        buf->resize(total);
    } catch (const std::bad_alloc&) {
        ERR("[DCP-CHUNK] alloc failed: obj={} total={}", object_name, total);
        return {false, nullptr, "", "", "Payload too large to buffer: " + object_name,
                ReadError::NETWORK};
    }
    CMVector<bool> filled(static_cast<size_t>(chunk_count), false);
    DataDigestMessage digest;

    // 读一个分片帧（帧头 + 子头 + small + raw 验 CRC）。
    // 返回：1=好片（seq/raw 填充）；2=DIGEST（digest 填充）；3=坏片（seq 填充）；
    // 0=连接断/协议失步；-1=帧头校验失败。
    auto read_frame = [&](uint32_t& seq, FlyBufferPtr& raw) -> int {
        char fh[9];
        if (!recv_exact(transport_.get(), fd, fh, 9)) return 0;
        uint64_t tl = 0;
        if (!parse_frame_header(fh, tl)) return -1;
        uint8_t type = static_cast<uint8_t>(fh[8]);
        if (type == static_cast<uint8_t>(MessageType::DATA_CHUNK)) {
            char sh[4];
            if (!recv_exact(transport_.get(), fd, sh, 4)) return 0;
            uint32_t small_len = read_be32(sh);
            if (small_len != ChunkFrameProtocol::kSmallFieldsLen) {
                return 0;  // 子头长度不符 = 协议失步
            }
            char sf[12];
            if (!recv_exact(transport_.get(), fd, sf, 12)) return 0;
            uint32_t fseq = 0;
            uint64_t fcrc = 0;
            ChunkFrameProtocol::parse_small_fields(sf, small_len, fseq, fcrc);
            uint64_t raw_len = ChunkFrameProtocol::raw_len_from_total(tl);
            if (fseq >= chunk_count || raw_len == 0 || raw_len > frame ||
                static_cast<uint64_t>(fseq) * frame + raw_len > total) {
                return 0;  // seq/raw_len 越界 = 协议失步
            }
            raw = CMMakeShared<FlyBuffer>();
            raw->resize(raw_len);
            if (!recv_exact(transport_.get(), fd, raw->data(), raw_len)) return 0;
            seq = fseq;
            if (data_checksum(raw->data(), raw->size()) != fcrc) return 3;  // 坏片
            return 1;
        }
        if (type == static_cast<uint8_t>(MessageType::DATA_DIGEST)) {
            uint64_t payload_len = tl - 1;
            CMString payload(static_cast<size_t>(payload_len), '\0');
            if (!recv_exact(transport_.get(), fd, payload.data(),
                            static_cast<size_t>(payload_len))) {
                return 0;
            }
            // MessageProtocol::decode 消费完整帧（9B 前缀 + payload）——重组。
            CMString frame_buf;
            frame_buf.assign(fh, 9);
            frame_buf += payload;
            if (!MessageProtocol::decode(frame_buf, digest)) return 0;
            return 2;
        }
        return 0;  // 未知帧类型
    };

    auto fill = [&](uint32_t seq, const FlyBufferPtr& raw) {
        if (filled[seq]) return;  // 重传帧与原始帧重复：幂等
        std::memcpy(buf->data() + static_cast<size_t>(seq) * frame,
                    raw->data(), raw->size());
        filled[seq] = true;
    };

    // 阶段 1：流接收直到 DIGEST（坏片记录，不中断——发送方不停流，§8.1）。
    bool digest_got = false;
    CMUnorderedSet<uint32_t> holes;
    while (!digest_got) {
        uint32_t seq = 0;
        FlyBufferPtr raw;
        int r = read_frame(seq, raw);
        if (r == 1) {
            fill(seq, raw);
        } else if (r == 2) {
            digest_got = true;
        } else if (r == 3) {
            ERR("[DCP-CHUNK] bad chunk frame CRC: obj={} seq={} — will request resend",
                object_name, seq);
            holes.insert(seq);
        } else if (r == -1) {
            ERR("[DCP-CHUNK] frame header check failed: obj={}", object_name);
            return {false, nullptr, "", "",
                    "Chunk stream header check failed for " + object_name,
                    ReadError::CHECKSUM};
        } else {
            return {false, nullptr, "", "",
                    "Connection lost receiving chunks for " + object_name,
                    ReadError::NETWORK};
        }
    }

    // 阶段 2：补洞（每 seq 一次 CHUNK_RESEND；重传后仍坏/断连 → CHECKSUM。
    // §5：校验失败后的任何失败不可接受）。
    for (uint32_t seq : holes) {
        if (filled[seq]) continue;
        ChunkResendMessage rs;
        rs.seq_ = seq;
        CMString encoded = MessageProtocol::encode(rs);
        if (!transport_->send_all(fd, encoded.data(), encoded.size())) {
            return {false, nullptr, "", "",
                    "Connection lost requesting resend for " + object_name,
                    ReadError::CHECKSUM};
        }
        // 等该 seq 的重传帧（server 单帧重发；忽略无关帧）。
        bool got_it = false;
        while (!got_it) {
            uint32_t rseq = 0;
            FlyBufferPtr raw;
            int r = read_frame(rseq, raw);
            if (r == 1 && rseq == seq) {
                fill(seq, raw);
                got_it = true;
            } else if (r == 3 && rseq == seq) {
                ERR("[DCP-FATAL-DATA-CORRUPTION] resent chunk still bad: obj={} seq={}",
                    object_name, seq);
                return {false, nullptr, "", "",
                        "Resent chunk still corrupt for " + object_name,
                        ReadError::CHECKSUM};
            } else if (r <= 0) {
                return {false, nullptr, "", "",
                        "Resend exchange failed for " + object_name,
                        ReadError::CHECKSUM};
            }
            // r==1 但 seq 不符（迟到的重复帧）→ 丢弃继续等。
        }
    }

    // 阶段 3：洞校验 + 根摘要端到端校验。
    for (uint64_t i = 0; i < chunk_count; ++i) {
        if (!filled[static_cast<size_t>(i)]) {
            ERR("[DCP-FATAL-DATA-CORRUPTION] missing chunk after resend: obj={} seq={}",
                object_name, i);
            return {false, nullptr, "", "",
                    "Missing chunk (no resend path) for " + object_name,
                    ReadError::CHECKSUM};
        }
    }
    if (data_checksum(buf->data(), buf->size()) != digest.root_crc_) {
        ERR("[DCP-FATAL-DATA-CORRUPTION] chunk stream digest mismatch: obj={}", object_name);
        return {false, nullptr, "", "",
                "Chunk stream digest mismatch for " + object_name,
                ReadError::CHECKSUM};
    }
    if (digest.chunk_count_ != chunk_count) {
        ERR("[DCP-CHUNK] digest chunk_count mismatch: obj={} meta={} digest={}",
            object_name, chunk_count, digest.chunk_count_);
        return {false, nullptr, "", "",
                "Chunk count mismatch for " + object_name,
                ReadError::CHECKSUM};
    }

    // py_name：client 侧从重组后的 record 尾部 trailer 解析（§4.4）。
    CMString py_name;
    {
        ObjectHeader hdr;
        size_t tl = 0;
        if (ObjectHeader::deserialize_trailer({buf->data(), buf->size()}, hdr, tl)) {
            py_name = hdr.py_name_;
        }
    }
    DBG("[DCP-CHUNK] success: obj={} total={} chunks={}", object_name, total, chunk_count);
    return {true, buf, py_name, meta.write_context_hash_, "", ReadError::NONE};
}

void DataClientPool::stop() {
    stopped_.store(true);
    {
        // 持锁 notify（同 release_slot 的 lost wakeup 论证）。
        std::lock_guard<std::mutex> lk(mutex_);
        slot_cv_.notify_all();
    }

    std::unique_lock<std::mutex> lk(mutex_);
    slot_cv_.wait(lk, [&] { return active_count_.load() == 0; });
    // 关闭并清空所有 keep-alive fd（此时无在飞 request 持有 fd）
    for (auto& [key, bucket] : buckets_) {
        for (auto& e : bucket) transport_->close(e.fd);
    }
    buckets_.clear();
    fd_to_peer_.clear();
    total_fd_count_ = 0;
}

}  // namespace fly
