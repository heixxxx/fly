#include <network/cpp/tcp_socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <cerrno>

namespace fly {

int TCPSocketTransport::create_listen_socket(const CMString& host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 128) < 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

int TCPSocketTransport::accept_connection(int listen_fd) {
    int fd = ::accept4(listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (fd < 0) return -1;
    set_nodelay(fd);
    return fd;
}

int TCPSocketTransport::create_connection(const CMString& host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        return -1;
    }

    set_nodelay(fd);
    return fd;
}

void TCPSocketTransport::set_nodelay(int fd) {
    int nodelay = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
}

void TCPSocketTransport::set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void TCPSocketTransport::set_recv_timeout(int fd, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void TCPSocketTransport::set_send_timeout(int fd, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

ssize_t TCPSocketTransport::send(int fd, const char* data, size_t len) {
    return ::send(fd, data, len, MSG_NOSIGNAL);
}

ssize_t TCPSocketTransport::recv(int fd, char* buf, size_t len) {
    return ::recv(fd, buf, len, 0);
}

bool TCPSocketTransport::send_all(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                int ret = ::poll(&pfd, 1, 5000);
                if (ret <= 0) return false;
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
                continue;
            }
            if (errno == EINTR) continue;
            return false;
        }
    }
    return true;
}

bool TCPSocketTransport::sendv(int fd, const struct iovec* iov, int iovcnt) {
    // Compute total bytes to send.
    size_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        total += iov[i].iov_len;
    }

    size_t sent = 0;
    struct iovec* iov_copy = const_cast<struct iovec*>(iov);
    int remaining_iovcnt = iovcnt;

    while (sent < total) {
        ssize_t n = ::writev(fd, iov_copy, remaining_iovcnt);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            // Advance iov pointers past sent data.
            while (n > 0 && remaining_iovcnt > 0) {
                if (static_cast<size_t>(n) >= iov_copy[0].iov_len) {
                    n -= iov_copy[0].iov_len;
                    iov_copy++;
                    remaining_iovcnt--;
                } else {
                    iov_copy[0].iov_base = static_cast<char*>(iov_copy[0].iov_base) + n;
                    iov_copy[0].iov_len -= n;
                    n = 0;
                }
            }
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                int ret = ::poll(&pfd, 1, 5000);
                if (ret <= 0) return false;
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
                continue;
            }
            if (errno == EINTR) continue;
            return false;
        }
    }
    return true;
}

int TCPSocketTransport::get_port(int fd) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        return -1;
    }
    return ntohs(addr.sin_port);
}

void TCPSocketTransport::close(int fd) {
    if (fd >= 0) {
        ::close(fd);
    }
}

CMSharedPtr<Transport> create_tcp_transport() {
    return CMMakeShared<TCPSocketTransport>();
}

}  // namespace fly
