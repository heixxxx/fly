#pragma once

#include <network/cpp/transport_interface.h>

namespace fly {

class TCPSocketTransport : public Transport {
public:
    TCPSocketTransport() = default;
    ~TCPSocketTransport() override = default;

    int create_listen_socket(const CMString& host, int port) override;
    int accept_connection(int listen_fd) override;
    int create_connection(const CMString& host, int port) override;

    void set_nodelay(int fd) override;
    void set_nonblocking(int fd) override;
    void set_recv_timeout(int fd, int timeout_ms) override;
    void set_send_timeout(int fd, int timeout_ms) override;

    ssize_t send(int fd, const char* data, size_t len) override;
    ssize_t recv(int fd, char* buf, size_t len) override;

    bool send_all(int fd, const char* data, size_t len) override;
    int get_port(int fd) override;

    void close(int fd) override;
};

CMSharedPtr<Transport> create_tcp_transport();

}  // namespace fly
