#include <agent/cpp/graceful_shutdown.h>

#include <atomic>
#include <csignal>

namespace fly {

namespace {
std::atomic<bool> g_graceful_shutdown{false};

extern "C" void graceful_shutdown_sigaction(int sig) {
    // async-signal-safe：只做 atomic 写。
    g_graceful_shutdown.store(true, std::memory_order_relaxed);
    (void)sig;
}
}  // namespace

void install_graceful_shutdown_handlers() {
    struct sigaction sa{};
    sa.sa_handler = graceful_shutdown_sigaction;
    sigemptyset(&sa.sa_mask);
    // SA_RESTART：可重启系统调用（read/poll 等）自动重启，由消费方在逻辑层
    // 观察标志退出，避免信号打断 IO 造成偶发 EINTR 错误路径。
    sa.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa, nullptr);
}

void set_graceful_shutdown() {
    g_graceful_shutdown.store(true, std::memory_order_relaxed);
}

bool graceful_shutdown_signalled() {
    return g_graceful_shutdown.load(std::memory_order_relaxed);
}

void reset_graceful_shutdown() {
    g_graceful_shutdown.store(false, std::memory_order_relaxed);
}

}  // namespace fly
