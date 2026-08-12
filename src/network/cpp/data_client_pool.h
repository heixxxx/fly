#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <common/cpp/fly_buffer.h>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace fly {

class Transport;

class DataClientPool {
public:
    /**
     * 使用传入的 Transport 实现。
     * pool_size 控制最大并发请求数。
     */
    explicit DataClientPool(CMSharedPtr<Transport> transport, int64_t pool_size = 2);

    /**
     * 便利构造函数：内部创建 TCPSocketTransport。
     * 仅用于测试和向后兼容。
     */
    explicit DataClientPool(int64_t pool_size = 2);

    ~DataClientPool();

    DataClientPool(const DataClientPool&) = delete;
    DataClientPool& operator=(const DataClientPool&) = delete;

    // Issue a single data read against one peer's DataServer.
    //
    // Returns (success, data, py_name, write_context_hash, error_message, read_error):
    //   - On success: success=true, data/py_name/hash populated.
    //   - On failure: success=false, read_error classifies the cause (drives the
    //     TIER2 retry policy in DataService::read_raw_compressed). error_message
    //     is the human-readable string for logging only.
    //
    // NOTE: DATA_NOT_READY is NO LONGER internally polled. It is returned as
    // ReadError::DATA_NOT_READY so the caller (TIER2) owns the backoff/retry
    // policy. The pool performs exactly one request per call.
    std::tuple<bool, FlyBufferPtr, CMString, CMString, CMString, ReadError> request(
        const CMString& host,
        int port,
        const CMString& object_name,
        uint64_t requesting_worker_id = 0,
        uint64_t request_id = 0,
        int timeout_ms = 300000);

    void stop();

private:
    CMSharedPtr<Transport> transport_;
    int64_t pool_size_;
    std::atomic<int> active_count_{0};
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::condition_variable slot_cv_;

    // ── keep-alive 连接池 ──
    // 复用 fd 避免高频请求时反复 socket()+connect()。同一 peer 维持多条连接以支持
    // 并发收数据（单 fd 同步 request-response 无法并行）。容量上限 2×pool_size：
    // in-use fd ≤ pool_size（受 slot 信号量约束），余量给 idle 缓冲。
    struct FdEntry {
        int fd = -1;
        std::chrono::steady_clock::time_point last_used;
        bool in_use = false;
    };
    std::unordered_map<CMString, std::vector<FdEntry>> buckets_;  // key = "host:port"
    std::unordered_map<int, CMString> fd_to_peer_;                // fd → key（release 定位）
    int total_fd_count_ = 0;
    int64_t max_fd_count_;                                        // = 2 * pool_size_
    static constexpr int kIdleTtlSec = 60;                        // idle fd 超时清理（防半开）

    int borrow_fd(const CMString& host, int port);                // 取得一个 fd（复用/新建/淘汰）
    void release_fd(int fd, bool healthy);                        // 归还：healthy=false 关闭移除
    bool probe_fd_health(int fd) const;                           // SO_ERROR 预检（不持锁）
    static CMString make_peer_key(const CMString& host, int port);
    // 以下均要求调用方已持有 mutex_
    int try_acquire_idle_locked(const CMString& key);
    bool evict_one_idle_locked();                                 // 反倾斜淘汰一个 idle
    void remove_fd_locked(int fd);                                // 从结构移除（不 close）
    void reap_expired_locked();                                   // TTL 清理过期 idle
};

}  // namespace fly
