#pragma once

#include <common/cpp/common_types.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <memory>

namespace fly {

using IOTask = std::function<void()>;
using CompletionCallback = std::function<void()>;

class IOThreadPool {
public:
    explicit IOThreadPool(int thread_count);
    ~IOThreadPool();
    
    void submit(IOTask task, CompletionCallback completion = nullptr);
    
    void process_completions();
    
    /// Wait until predicate returns true, processing completions between checks.
    /// Returns true if predicate became true, false on timeout.
    bool wait_for_completion(std::function<bool()> predicate, int timeout_ms = 5000);
    
    void start();
    void stop();
    
    int queue_size() const;
    bool is_idle() const;

private:
    int thread_count_;
    CMVector<std::thread> workers_;
    
    std::queue<std::pair<IOTask, CompletionCallback>> tasks_;
    mutable std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;
    
    CMVector<CompletionCallback> completions_;
    std::mutex completions_mutex_;
    std::condition_variable completion_cv_;
    
    std::atomic<bool> running_{false};
    std::atomic<int> active_tasks_{0};
    
    void worker_loop();
};

}  // namespace fly