#include <network/cpp/io_thread_pool.h>
#include <chrono>

namespace fly {

IOThreadPool::IOThreadPool(int thread_count)
    : thread_count_(thread_count) {
}

IOThreadPool::~IOThreadPool() {
    stop();
}

void IOThreadPool::submit(IOTask task, CompletionCallback completion) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.emplace(std::move(task), std::move(completion));
    }
    tasks_cv_.notify_one();
    active_tasks_++;
}

void IOThreadPool::process_completions() {
    CMVector<CompletionCallback> to_process;
    {
        std::lock_guard<std::mutex> lock(completions_mutex_);
        to_process = std::move(completions_);
        completions_.clear();
    }
    
    for (auto& cb : to_process) {
        if (cb) {
            cb();
        }
    }
}

bool IOThreadPool::wait_for_completion(std::function<bool()> predicate, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        process_completions();
        if (predicate()) return true;
        
        std::unique_lock<std::mutex> lock(completions_mutex_);
        if (completion_cv_.wait_until(lock, deadline) == std::cv_status::timeout) break;
    }
    process_completions();
    return predicate();
}

void IOThreadPool::start() {
    running_ = true;
    
    workers_.reserve(thread_count_);
    for (int i = 0; i < thread_count_; i++) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

void IOThreadPool::stop() {
    running_ = false;
    
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_cv_.notify_all();
    }
    
    for (auto& thread : workers_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workers_.clear();
}

int IOThreadPool::queue_size() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    return static_cast<int>(tasks_.size());
}

bool IOThreadPool::is_idle() const {
    return queue_size() == 0 && active_tasks_.load() == 0;
}

void IOThreadPool::worker_loop() {
    while (running_) {
        std::pair<IOTask, CompletionCallback> item;
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this] { 
                return !tasks_.empty() || !running_; 
            });
            
            if (!running_ && tasks_.empty()) {
                return;
            }
            
            if (!tasks_.empty()) {
                item = std::move(tasks_.front());
                tasks_.pop();
            } else {
                continue;
            }
        }
        
        if (item.first) {
            item.first();
        }
        active_tasks_--;
        
        {
            std::lock_guard<std::mutex> lock(completions_mutex_);
            if (item.second) {
                completions_.push_back(std::move(item.second));
            }
            completion_cv_.notify_all();
        }
    }
}

}  // namespace fly