#pragma once

#include <network/cpp/transport.h>
#include <mutex>

namespace fly {

class TCPTransport : public TransportLayer {
public:
    TCPTransport();
    ~TCPTransport() override;
    
    void listen(const CMString& address, int port) override;
    void stop_listening() override;
    uint64_t connect(const CMString& address, int port) override;
    ssize_t send(uint64_t conn_id, const CMString& data) override;
    ssize_t recv(uint64_t conn_id, CMString& buffer, size_t max_size) override;
    CMVector<TransportEvent> poll(int timeout_ms) override;
    void close(uint64_t conn_id) override;
    void close_all() override;
    bool is_connected(uint64_t conn_id) const override;
    size_t connection_count() const override;
    int get_bound_port() const override;

private:
    int epoll_fd_ = -1;
    int listen_fd_ = -1;
    uint64_t next_conn_id_ = 1;
    
    // Protects conn_to_fd_ and fd_to_conn_ — accessed from reactor thread
    // (poll/unregister) and task threads (send/recv)
    mutable std::mutex conn_mutex_;
    CMUnorderedMap<uint64_t, int> conn_to_fd_;
    CMUnorderedMap<int, uint64_t> fd_to_conn_;
    
    uint64_t register_connection(int fd);
    void unregister_connection(uint64_t conn_id);
    void set_nonblocking(int fd);
    CMString drain_socket(int fd, size_t max_size);
};

}  // namespace fly