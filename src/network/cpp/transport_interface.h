#pragma once

#include <common/cpp/common_types.h>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <sys/uio.h>

namespace fly {

class Transport {
public:
    virtual ~Transport() = default;

    virtual int create_listen_socket(const CMString& host, int port) = 0;
    virtual int accept_connection(int listen_fd) = 0;
    virtual int create_connection(const CMString& host, int port) = 0;

    virtual void set_nodelay(int fd) = 0;
    virtual void set_nonblocking(int fd) = 0;
    virtual void set_recv_timeout(int fd, int timeout_ms) = 0;
    virtual void set_send_timeout(int fd, int timeout_ms) = 0;

    virtual ssize_t send(int fd, const char* data, size_t len) = 0;
    virtual ssize_t recv(int fd, char* buf, size_t len) = 0;

    virtual bool send_all(int fd, const char* data, size_t len) = 0;

    // Scatter-gather send: write multiple buffers in a single system call (writev).
    // Returns true if all data was sent successfully.
    virtual bool sendv(int fd, const struct iovec* iov, int iovcnt) = 0;

    // Zero-copy file→socket send (sendfile(2)): streams len bytes from file_fd
    // starting at offset to fd. Returns true iff all len bytes were queued;
    // offset is not modified. Only meaningful for transports whose fds are
    // TCP sockets (sendfile target); other transports return false.
    virtual bool send_file(int fd, int file_fd, uint64_t offset, size_t len) = 0;

    virtual int get_port(int fd) = 0;

    virtual void close(int fd) = 0;
};

// Read exactly n bytes from fd (partial-read loop). Returns false on EOF/error.
// Shared by DataClientPool and MetadataClient for frame reads.
inline bool recv_exact(Transport* transport, int fd, char* buf, size_t n) {
    size_t received = 0;
    while (received < n) {
        ssize_t r = transport->recv(fd, buf + received, n - received);
        if (r <= 0) return false;
        received += static_cast<size_t>(r);
    }
    return true;
}

}  // namespace fly
