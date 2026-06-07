#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <deque>
#include <mutex>
#include <atomic>
#include <tuple>

namespace fly {

class DataClientPool;

// RAII wrapper: destructor returns fd to pool (or closes if pool full/stopped/invalidate'd).
class PooledConnection {
public:
    PooledConnection() = default;
    PooledConnection(int fd, const CMString& host, int port, DataClientPool* pool);
    PooledConnection(PooledConnection&& other) noexcept;
    PooledConnection& operator=(PooledConnection&& other) noexcept;
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;
    ~PooledConnection();

    int fd() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    void invalidate();

private:
    int fd_ = -1;
    CMString host_;
    int port_ = 0;
    DataClientPool* pool_ = nullptr;
};

// Thread-safe connection pool keyed by (host, port).  Reuses idle TCP fds
// across requests instead of creating a new socket each time.
class DataClientPool {
public:
    explicit DataClientPool(int64_t max_pool_size = 4);
    ~DataClientPool();

    DataClientPool(const DataClientPool&) = delete;
    DataClientPool& operator=(const DataClientPool&) = delete;

    PooledConnection acquire(const CMString& host, int port, int timeout_ms = 300000);

    void release(int fd, const CMString& host, int port);

    // Returns (success, compressed_bytes, py_name, write_context_hash, error_message).
    std::tuple<bool, CMString, CMString, CMString, CMString> request(
        const CMString& host,
        int port,
        const CMString& object_name,
        int timeout_ms = 300000);

    void stop();

private:
    int create_connection(const CMString& host, int port, int timeout_ms);

    using PoolKey = std::tuple<CMString, int>;

    CMUnorderedMap<PoolKey, std::deque<int>> pools_;
    std::mutex mutex_;
    std::atomic<bool> stopped_{false};
    int64_t max_pool_size_;
};

}  // namespace fly
