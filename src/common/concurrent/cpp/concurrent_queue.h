#pragma once

#include <container/cpp/container_aliases.h>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace fly {

// 容量策略：有界队列的记账口径（按元素个数 / 按字节）。limit()==0 = 无界。
struct CountCapacity {
    explicit CountCapacity(size_t max_items = 0) : limit_(max_items) {}
    template <typename T>
    size_t size_of(const T&) const {
        return 1;
    }
    size_t limit_;
};

// 按字节计（T 需提供 size()）：背压口径与 wire/内存消耗一致。
struct BytesCapacity {
    explicit BytesCapacity(size_t max_bytes = 0) : limit_(max_bytes) {}
    template <typename T>
    size_t size_of(const T& v) const {
        return v.size();
    }
    size_t limit_;
};

// 线程安全有界队列（§13.1 队列语义的官方封装）：生产者-消费者 + 背压 +
// 终态关闭。mutex+deque+双 cv 全部内聚；notify 全部持锁调用（§13.2）。
//
// 语义：
//   - push 阻塞于容量上界（背压传播到生产者）；close/fail 后返回 false
//     （流已死，生产方放弃）。
//   - pop 阻塞于非空；close 且排空后返回 nullopt（EOF）；close 且未排空
//     仍可继续 pop（消费方决定是否排空残量——优雅关停语义）。
//   - drain_all 原子取走全部（重放/批量上报场景）；snapshot 拷贝不清空。
//   - fail() 单向置错：语义同 close，但 pop 立即失败（错误流快速放弃）。
template <typename T, typename Capacity = CountCapacity>
class ConcurrentQueue {
public:
    explicit ConcurrentQueue(Capacity cap = Capacity{}) : cap_(cap) {}

    ConcurrentQueue(const ConcurrentQueue&) = delete;
    ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

    // 生产：容量满则阻塞等待空间。返回 false = 队列已 close/fail（丢弃）。
    bool push(T value) {
        const size_t sz = cap_.size_of(value);
        std::unique_lock<std::mutex> lk(mutex_);
        space_cv_.wait(lk, [&] {
            return closed_ || failed_ || cap_.limit_ == 0 ||
                   occupancy_ + sz <= cap_.limit_;
        });
        if (closed_ || failed_) return false;
        occupancy_ += sz;
        queue_.push_back(std::move(value));
        data_cv_.notify_one();   // 持锁 notify（§13.2）
        return true;
    }

    // 非阻塞生产：满即返回 false（调用方自旋/退避或放弃）。
    bool try_push(T value) {
        const size_t sz = cap_.size_of(value);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (closed_ || failed_) return false;
            if (cap_.limit_ != 0 && occupancy_ + sz > cap_.limit_) {
                return false;
            }
            occupancy_ += sz;
            queue_.push_back(std::move(value));
            data_cv_.notify_one();
        }
        return true;
    }

    // 消费：空则阻塞。close 且排空 → nullopt；fail → nullopt（即使有残量）。
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lk(mutex_);
        data_cv_.wait(lk, [&] {
            return !queue_.empty() || closed_ || failed_;
        });
        if (queue_.empty() || failed_) return std::nullopt;
        auto out = std::optional<T>(std::move(queue_.front()));
        queue_.pop_front();
        occupancy_ -= cap_.size_of(*out);
        space_cv_.notify_all();   // 持锁 notify（§13.2）
        return out;
    }

    // 带超时消费：超时返回 nullopt（调用方以 closed()/size() 区分 EOF）。
    std::optional<T> pop_for(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lk(mutex_);
        data_cv_.wait_for(lk, timeout, [&] {
            return !queue_.empty() || closed_ || failed_;
        });
        if (queue_.empty() || failed_) return std::nullopt;
        auto out = std::optional<T>(std::move(queue_.front()));
        queue_.pop_front();
        occupancy_ -= cap_.size_of(*out);
        space_cv_.notify_all();
        return out;
    }

    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (queue_.empty() || failed_) return std::nullopt;
        auto out = std::optional<T>(std::move(queue_.front()));
        queue_.pop_front();
        occupancy_ -= cap_.size_of(*out);
        space_cv_.notify_all();
        return out;
    }

    // 原子取走全部（重放/flush 场景）。
    std::vector<T> drain_all() {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<T> out(std::make_move_iterator(queue_.begin()),
                           std::make_move_iterator(queue_.end()));
        queue_.clear();
        occupancy_ = 0;
        space_cv_.notify_all();
        return out;
    }

    // 拷贝快照不清空（观察/成组发送拷贝语义；T 需可拷贝）。
    std::vector<T> snapshot() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return std::vector<T>(queue_.begin(), queue_.end());
    }

    // 清空（丢弃语义，非终态——后续 push/pop 照常）。
    void clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        queue_.clear();
        occupancy_ = 0;
        space_cv_.notify_all();
    }

    // 终态：唤醒全部等待者；push 拒绝新元素，pop 排空残量后给 EOF。
    void close() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            closed_ = true;
        }
        data_cv_.notify_all();
        space_cv_.notify_all();
    }

    // 错误终态：同 close 但 pop 立即放弃残量（错误流快速失败）。
    void fail() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            failed_ = true;
        }
        data_cv_.notify_all();
        space_cv_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return queue_.size();
    }
    size_t occupancy() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return occupancy_;
    }
    bool closed() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return closed_;
    }
    bool failed() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return failed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable space_cv_;   // 生产等空间（有界时）
    std::condition_variable data_cv_;    // 消费等数据/终态
    std::deque<T> queue_;
    size_t occupancy_ = 0;   // cap_ 记账口径的当前占用
    Capacity cap_;
    bool closed_ = false;
    bool failed_ = false;
};

}  // namespace fly
