#include <network/cpp/data_client_pool.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_quality_monitor.h>
#include <log/cpp/logger.h>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
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

std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString, ReadError> DataClientPool::request(
    const CMString& host,
    int port,
    const CMString& object_name,
    uint64_t requesting_worker_id,
    uint64_t request_id,
    int timeout_ms)
{
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

        // 1. Read 5B frame header [4B total_len][1B type]
        char frame_header[5];
        if (!recv_exact(transport_.get(), fd, frame_header, 5)) {
            ERR("[DCP] recv header failed: obj={} fd={} errno={}", object_name, fd, errno);
            release_fd(fd, false);
            release_slot();
            return {false, nullptr, "", "",
                    "Connection lost for " + object_name,
                    ReadError::NETWORK};
        }
        uint32_t total_len = read_be32(frame_header);
        if (total_len < 6 || total_len > 256 * 1024 * 1024) {
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

        // 4. If has_raw: read raw payload directly into FlyBuffer
        FlyBufferPtr data_buf;
        if (has_raw) {
            uint32_t raw_len = DataResponseProtocol::raw_len_from_total(total_len, small_fields_len);
            data_buf = CMMakeShared<FlyBuffer>();
            data_buf->resize(raw_len);
            if (!recv_exact(transport_.get(), fd, data_buf->data(), raw_len)) {
                ERR("[DCP] recv raw failed: obj={} fd={} errno={}", object_name, fd, errno);
                release_fd(fd, false);
                release_slot();
                return {false, nullptr, "", "",
                        "Connection lost receiving payload for " + object_name,
                        ReadError::NETWORK};
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
