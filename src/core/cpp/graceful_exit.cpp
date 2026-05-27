#include <core/cpp/graceful_exit.h>
#include <cstdlib>
#include <atomic>
#include <functional>

namespace fly {

namespace {

std::atomic<bool> exit_initiated{false};
std::function<void()> shutdown_callback;

}  // namespace

void register_shutdown_callback(std::function<void()> callback) {
    shutdown_callback = std::move(callback);
}

void graceful_exit(const CMString& reason, TaskErrorType error_type, int exit_code) {
    if (exit_initiated.exchange(true)) return;

    fprintf(stderr, "[GRACEFUL EXIT] %s (error_type=%d, exit_code=%d)\n",
            reason.c_str(), static_cast<int>(error_type), exit_code);

    if (shutdown_callback) {
        shutdown_callback();
    }

    _exit(exit_code);
}

}  // namespace fly
