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

        task.execute();
        task.on_complete();

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
