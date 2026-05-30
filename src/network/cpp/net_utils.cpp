#include <network/cpp/net_utils.h>
#include <sys/socket.h>
#include <chrono>
#include <cstring>

namespace fly {

bool net_recv_exact(int fd, char* buf, size_t len, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    size_t received = 0;
    while (received < len) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) return false;
        
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(remaining / 1000);
        tv.tv_usec = static_cast<suseconds_t>((remaining % 1000) * 1000);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        ssize_t n = ::recv(fd, buf + received, len - received, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

bool net_send_all(int fd, const char* data, size_t len, int /*timeout_ms*/) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace fly
