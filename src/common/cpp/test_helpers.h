#pragma once

#include <chrono>
#include <thread>
#include <functional>

namespace fly {
namespace test {

inline void wait_for(std::function<bool()> cond, int max_iters = 100, int interval_ms = 10) {
    for (int i = 0; i < max_iters; ++i) {
        if (cond()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

template<typename T>
void wait_for_running(T& agent, bool expected, int max_iters = 10, int interval_ms = 10) {
    wait_for([&]{ return agent.is_running() == expected; }, max_iters, interval_ms);
}

inline bool wait_until_registered(auto& worker, int max_attempts = 100, int interval_ms = 10) {
    for (int i = 0; i < max_attempts; ++i) {
        if (worker.is_registered()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    return worker.is_registered();
}

}  // namespace test
}  // namespace fly
