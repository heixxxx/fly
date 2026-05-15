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
    
    std::atomic<bool> running_{false};
    std::atomic<int> active_tasks_{0};
    
    void worker_loop();
};

}  // namespace fly