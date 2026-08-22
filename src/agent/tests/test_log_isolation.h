#pragma once

// MasterAgent 测试进程的 log_dir 隔离。
//
// 背景：bazel 并行跑多个测试二进制时，直接构造 MasterAgent 的测试都会
// 以 Config 默认 "fly_log" 打开 {log_dir}/monitor.db（SQLite 写锁）——
// 并行进程共享源码树同一 DB 文件导致 "database is locked" 互踩。
//
// 本头文件经静态初始化器（先于 gtest_main 生效）把 Config log_dir 指到
// 进程唯一临时目录。测试内再 set_str("log_dir", TempDir) 的（如
// StopWritesSummaryFiles）自然覆盖本默认值。临时目录不做自清理（测试
// 产物，OS /tmp 策略管理；与各测试文件 TempDir 自清理的约定独立）。
#include <core/cpp/config.h>

#include <cstdio>
#include <filesystem>
#include <unistd.h>

namespace fly {
namespace test {

struct LogDirIsolation {
    LogDirIsolation() {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "/tmp/fly_test_log_%d",
                      static_cast<int>(::getpid()));
        std::filesystem::create_directories(buf);
        Config::instance()->set_str("log_dir", buf);
    }
};

inline LogDirIsolation g_log_dir_isolation;

}  // namespace test
}  // namespace fly
