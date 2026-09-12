#pragma once

#include <container/cpp/container_aliases.h>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
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

// fatal 终局断言（MSG_FATAL_EXIT 不返回语义的统一断言设施）：fork 子进程
// 执行 trigger（内部走 fatal_exit → _exit(expected_code)），父进程有界
// waitpid 断言子进程正常退出且退出码正确。子进程内禁用 gtest 断言（fork
// 后 gtest 状态不回传）：trigger 未生效 → 以 0 退出 → 父侧断言失败；
// 意外死锁（fork 只复制调用线程，子进程可能卡在被其他线程持有的锁上）
// → 30s deadline 后 SIGKILL 收尸（零容忍：测试本身不允许挂死）。
inline void expect_fatal_exit_code(const std::function<void()>& trigger,
                                   int expected_code) {
    pid_t pid = fork();
    ASSERT_GE(pid, 0) << "fork failed";
    if (pid == 0) {
        trigger();
        _exit(0);  // 触发未生效（未退出）→ 0 → 父侧断言失败
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    pid_t done = 0;
    while ((done = waitpid(pid, &status, WNOHANG)) == 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            FAIL() << "fatal child process hung (killed after 30s)";
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_GT(done, 0) << "waitpid failed";
    ASSERT_TRUE(WIFEXITED(status)) << "child did not exit normally (signal "
                                   << (WIFSIGNALED(status) ? WTERMSIG(status) : 0) << ")";
    ASSERT_EQ(WEXITSTATUS(status), expected_code)
        << "fatal path must _exit(" << expected_code << ")";
};

}  // namespace test
}  // namespace fly
