#pragma once

#include <common/cpp/common_types.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <thread>

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

// 测试专用临时目录生成（替代 /tmp/fly_test_* 硬编码——/tmp 无限累积是
// WSL2 磁盘事故同型风险，见 AGENTS.md「/tmp 禁令」）。
//
// 基目录优先级：
//   1. TEST_TMPDIR（bazel test 沙箱自动提供，测试结束 bazel 自动回收）；
//   2. .work/gtest_tmp/（手动直跑二进制时——cwd 即仓库根；.work 按仓库
//      惯例任务后清理）。
// 路径含 pid + 时间戳 + 进程内序号，防并行/同进程多次生成冲突。
inline CMString qa_tmp_dir(const CMString& hint) {
    static std::atomic<uint64_t> seq{0};
    const char* base = ::getenv("TEST_TMPDIR");
    CMString dir = base ? CMString(base) : CMString(".work/gtest_tmp");
    dir += "/" + hint + "_" + std::to_string(::getpid()) + "_" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
           "_" + std::to_string(seq.fetch_add(1));
    return dir;
}

// RAII 临时目录：构造即创建，析构递归删除（fixture 成员或局部作用域用）。
struct ScopedTempDir {
    CMString path;
    explicit ScopedTempDir(const CMString& hint) : path(qa_tmp_dir(hint)) {
        std::filesystem::create_directories(path);
    }
    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);   // 清理失败不掩盖测试结果
    }
    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;
};

}  // namespace test
}  // namespace fly
