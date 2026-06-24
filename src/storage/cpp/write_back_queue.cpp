#include <storage/cpp/write_back_queue.h>

namespace fly {

WriteBackQueue::WriteBackQueue(size_t high_watermark)
    : high_watermark_(high_watermark) {}

WriteBackQueue::~WriteBackQueue() {
    stop();
}

void WriteBackQueue::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    worker_ = std::thread(&WriteBackQueue::worker_loop, this);
}

void WriteBackQueue::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    cv_not_empty_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool WriteBackQueue::is_running() const {
    return running_;
}

void WriteBackQueue::enqueue(WriteRequest&& task) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() > high_watermark_) {
            cv_backpressure_.wait(lock, [this] {
                return queue_.empty();
            });
        }
        queue_.push_back(std::move(task));
        pending_++;
    }
    cv_not_empty_.notify_one();
}

void WriteBackQueue::drain() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_drained_.wait(lock, [this] {
        return queue_.empty() && pending_ == 0;
    });
}

void WriteBackQueue::clear_pending() {
    size_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dropped = queue_.size();
        queue_.clear();
        // 递减被丢弃请求的 pending_ 计数（enqueue 时 pending_++ 过）。
        // 不在 worker_loop 执行的请求的 on_complete_ 不会执行，调用方需
        // 自行清理 DataService/ObjectCache。注意：正在 worker_loop 执行的那
        // 个（已 pop 出 queue）不在 queue_ 内，其 pending_ 由 worker_loop
        // 完成后正常递减。
        if (pending_ >= dropped) {
            pending_ -= dropped;
        } else {
            pending_ = 0;
        }
    }
    // 队列已空，唤醒 drain / backpressure 等待者。
    // （正在执行的那个的 pending_ 仍 >0，drain 会等它完成；若它也已被某种
    // 方式终止，pending_ 归零后 cv_drained_ 唤醒。）
    cv_drained_.notify_all();
    cv_backpressure_.notify_all();
    (void)dropped;
}

size_t WriteBackQueue::pending_count() const {
    return pending_;
}

void WriteBackQueue::worker_loop() {
    while (true) {
        WriteRequest task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_not_empty_.wait(lock, [this] {
                return !queue_.empty() || !running_;
            });

            if (queue_.empty() && !running_) {
                break;
            }

            if (!queue_.empty()) {
                task = std::move(queue_.front());
                queue_.pop_front();
            } else {
                continue;
            }
        }

        task.execute_();
        task.on_complete_();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_--;
            if (queue_.empty()) {
                cv_drained_.notify_all();
                cv_backpressure_.notify_all();
            }
        }
    }
}

}  // namespace fly
