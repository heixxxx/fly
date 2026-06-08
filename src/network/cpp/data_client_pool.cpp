#include <network/cpp/data_client_pool.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_utils.h>
#include <log/cpp/logger.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

namespace fly {

// ============================================================
// PooledConnection
// ============================================================

PooledConnection::PooledConnection(int fd, const CMString& host, int port, DataClientPool* pool)
    : fd_(fd), host_(host), port_(port), pool_(pool) {}

PooledConnection::PooledConnection(PooledConnection&& other) noexcept
    : fd_(other.fd_), host_(std::move(other.host_)),
      port_(other.port_), pool_(other.pool_) {
    other.fd_ = -1;
    other.pool_ = nullptr;
}

PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0 && pool_) {
            pool_->release(fd_, host_, port_);
        } else if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = other.fd_;
        host_ = std::move(other.host_);
        port_ = other.port_;
        pool_ = other.pool_;
        other.fd_ = -1;
        other.pool_ = nullptr;
    }
    return *this;
}

PooledConnection::~PooledConnection() {
    if (fd_ >= 0) {
        if (pool_) {
            pool_->release(fd_, host_, port_);
        } else {
            ::close(fd_);
        }
    }
}

void PooledConnection::invalidate() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    pool_ = nullptr;
}

// ============================================================
// DataClientPool
// ============================================================

DataClientPool::DataClientPool(int64_t max_pool_size)
    : max_pool_size_(max_pool_size) {}

DataClientPool::~DataClientPool() {
    stop();
}

int DataClientPool::create_connection(const CMString& host, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

PooledConnection DataClientPool::acquire(const CMString& host, int port, int timeout_ms) {
    if (stopped_.load(std::memory_order_relaxed) || max_pool_size_ == 0) {
        int fd = create_connection(host, port, timeout_ms);
        return PooledConnection(fd, host, port, nullptr);
    }

    PoolKey key{host, port};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pools_.find(key);
        if (it != pools_.end() && !it->second.empty()) {
            int fd = it->second.front();
            it->second.pop_front();
            return PooledConnection(fd, host, port, this);
        }
    }

    int fd = create_connection(host, port, timeout_ms);
    return PooledConnection(fd, host, port, fd >= 0 ? this : nullptr);
}

void DataClientPool::release(int fd, const CMString& host, int port) {
    if (fd < 0) return;

    if (stopped_.load(std::memory_order_relaxed) || max_pool_size_ == 0) {
        ::close(fd);
        return;
    }

    PoolKey key{host, port};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& deque = pools_[key];
        if (static_cast<int64_t>(deque.size()) < max_pool_size_) {
            deque.push_back(fd);
            return;
        }
    }

    ::close(fd);
}

std::tuple<bool, CMString, CMString, CMString, CMString> DataClientPool::request(
    const CMString& host,
    int port,
    const CMString& object_name,
    uint64_t requesting_worker_id,
    uint64_t request_id,
    int timeout_ms)
{
    PooledConnection conn = acquire(host, port, timeout_ms);
    if (!conn.valid()) {
        return {false, "", "", "", "Failed to create pooled connection to " + host + ":" + std::to_string(port)};
    }

    DataRequestMessage req;
    req.object_name = object_name;
    req.requesting_worker_id = requesting_worker_id;
    req.request_id = request_id;
    CMString encoded_req = MessageProtocol::encode(req);

    if (!net_send_all(conn.fd(), encoded_req.data(), encoded_req.size())) {
        conn.invalidate();
        return {false, "", "", "", "Failed to send compressed request for " + object_name};
    }

    char header[5] = {};
    if (!net_recv_exact(conn.fd(), header, 5, timeout_ms)) {
        conn.invalidate();
        return {false, "", "", "", "Timeout receiving response header for " + object_name};
    }

    uint32_t total_len =
        (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
        (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
        static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

    if (total_len < 1 || total_len > 256 * 1024 * 1024) {
        conn.invalidate();
        return {false, "", "", "", "Invalid response frame size for " + object_name};
    }

    uint32_t payload_len = total_len - 1;

    CMString payload(payload_len, '\0');
    if (payload_len > 0 && !net_recv_exact(conn.fd(), payload.data(), payload_len, timeout_ms)) {
        conn.invalidate();
        return {false, "", "", "", "Timeout receiving response payload for " + object_name};
    }

    CMString full_buf;
    full_buf.resize(4 + total_len);
    std::memcpy(&full_buf[0], header, 5);
    if (payload_len > 0) {
        std::memcpy(&full_buf[5], payload.data(), payload_len);
    }

    DataResponseMessage response;
    if (!MessageProtocol::decode(full_buf, response)) {
        conn.invalidate();
        return {false, "", "", "", "Failed to decode response for " + object_name};
    }

    return {response.success, response.compressed_data, response.py_name,
            response.write_context_hash, response.error_message};
}

void DataClientPool::stop() {
    stopped_.store(true, std::memory_order_relaxed);
    CMUnorderedMap<PoolKey, std::deque<int>> to_close;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        to_close = std::move(pools_);
        pools_.clear();
    }
    for (auto& [key, deque] : to_close) {
        for (int fd : deque) {
            ::close(fd);
        }
    }
}

}  // namespace fly
