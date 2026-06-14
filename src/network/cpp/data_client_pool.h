#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <tuple>

namespace fly {

class DataClientPool {
public:
    explicit DataClientPool(int64_t pool_size = 2);
    ~DataClientPool();

    DataClientPool(const DataClientPool&) = delete;
    DataClientPool& operator=(const DataClientPool&) = delete;

    std::tuple<bool, CMString, CMString, CMString, CMString> request(
        const CMString& host,
        int port,
        const CMString& object_name,
        uint64_t requesting_worker_id = 0,
        uint64_t request_id = 0,
        int timeout_ms = 300000);

    void stop();

private:
    int64_t pool_size_;
    std::atomic<int> active_count_{0};
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::condition_variable slot_cv_;

    static int create_connection(const CMString& host, int port);
};

}  // namespace fly
