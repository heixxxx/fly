#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/error_types.h>
#include <serialization/cpp/fly_buffer.h>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <tuple>

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
};

}  // namespace fly
