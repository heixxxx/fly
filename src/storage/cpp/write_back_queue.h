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

    // 丢弃所有未处理的写请求（已在 worker_loop 执行的那个会自然完成），递减
    // 它们的 pending_ 计数，并唤醒 drain/backpressure 等待者。用于 task 异常
    // 清理：脏数据本要丢弃，无需先落盘再 truncate。被丢弃请求的 on_complete_
    // 不会执行，调用方需自行清理 DataService/ObjectCache。
    void clear_pending();

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
