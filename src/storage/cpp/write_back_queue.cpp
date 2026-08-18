#include <storage/cpp/write_back_queue.h>
#include <log/cpp/logger.h>
#include <core/cpp/graceful_exit.h>
#include <common/cpp/error_types.h>

namespace fly {

namespace {
// 落盘失败的最大重试次数（瞬时 IO 错误，如短暂的系统调用中断）。
// 超过后视为确定性失败（磁盘满/权限/硬件），graceful_exit 避免静默数据丢失。
constexpr int kMaxWriteBackRetries = 3;
}  // namespace

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
        // 持锁 notify：worker_loop 的 cv_not_empty_ 是无超时谓词 wait，锁外
        // notify 存在 lost wakeup 窗口（谓词检查后、进入 wait 前 notify 落空
        // → 落盘线程永睡，后续 write 全部滞留内存）。
        cv_not_empty_.notify_one();
    }
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
    // 队列已空，唤醒 drain / backpressure 等待者（持锁 notify：drain() 的
    // cv_drained_ 是无超时谓词 wait，锁外 notify 落空会让关闭期 drain 永挂）。
    // （正在执行的那个的 pending_ 仍 >0，drain 会等它完成；若它也已被某种
    // 方式终止，pending_ 归零后 cv_drained_ 唤醒。）
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cv_drained_.notify_all();
        cv_backpressure_.notify_all();
    }
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

        // P1-8: write-back 错误处理。
        // 1) try-catch 防止 execute_/on_complete_ 抛异常导致 worker 线程崩溃
        //    → pending_ 永不递减 → drain() 永久阻塞（~Database 死锁）。
        // 2) execute_ 返回 bool：成功 → on_complete_（标记 COMPLETE）；
        //    失败 → on_error_（标记失败）+ 重试瞬时错误 + 确定性失败 graceful_exit。
        bool write_ok = false;
        try {
            for (int attempt = 0; attempt <= kMaxWriteBackRetries; ++attempt) {
                write_ok = task.execute_();
                if (write_ok) break;
                if (attempt < kMaxWriteBackRetries) {
                    ERR("[WRITE-BACK] disk write failed, retrying ({}/{})",
                        attempt + 1, kMaxWriteBackRetries);
                }
            }
        } catch (const std::exception& e) {
            ERR("[WRITE-BACK] execute_ threw exception: {}", e.what());
        } catch (...) {
            ERR("[WRITE-BACK] execute_ threw unknown exception");
        }

        if (write_ok) {
            try {
                if (task.on_complete_) task.on_complete_();
            } catch (const std::exception& e) {
                ERR("[WRITE-BACK] on_complete_ threw exception: {}", e.what());
            } catch (...) {
                ERR("[WRITE-BACK] on_complete_ threw unknown exception");
            }
        } else {
            // 落盘确定失败（重试耗尽）。通知 DataService 标记对象不可用，
            // 然后 graceful_exit —— 数据完整性已被破坏，继续运行会让后续读
            // 拿到"已注册但未落盘"的对象（local_idx 声称 COMPLETE 但实际丢失）。
            ERR("[WRITE-BACK] persistent disk write failure after {} retries — "
                "data integrity at risk, initiating graceful exit", kMaxWriteBackRetries);
            try {
                if (task.on_error_) task.on_error_();
            } catch (...) {
                // on_error_ 失败不阻塞退出决策
            }
            fly::graceful_exit(
                "write-back persistent disk failure: object cannot be persisted "
                "(disk full / IO error / permission denied)",
                fly::TaskErrorType::UNKNOWN, 1);
        }

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
