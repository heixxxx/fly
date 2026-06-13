#pragma once

#include <common/cpp/common_types.h>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <deque>
#include <atomic>

namespace fly {

struct WriteRequest {
    std::function<void()> execute_;      // Does the actual write (calls DataWriter methods)
    std::function<void()> on_complete_;   // Notifies DataService + master after write
};

class WriteBackQueue {
public:
    explicit WriteBackQueue(size_t high_watermark = 10);
    ~WriteBackQueue();

    WriteBackQueue(const WriteBackQueue&) = delete;
    WriteBackQueue& operator=(const WriteBackQueue&) = delete;

    void start();
    void stop();
    bool is_running() const;

    // Enqueue a write request (move semantics, zero-copy).
    // If queue size > high_watermark, blocks until queue is COMPLETELY EMPTY, then enqueues.
    void enqueue(WriteRequest&& task);

    // Wait for all pending writes to complete
    void drain();

    size_t pending_count() const;

private:
    void worker_loop();

    size_t high_watermark_;
    std::deque<WriteRequest> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_drained_;
    std::condition_variable cv_backpressure_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> pending_{0};
};

}  // namespace fly
