#pragma once

#include <network/cpp/connection_manager.h>
#include <network/cpp/epoll_multiplexer.h>
#include <network/cpp/transport_interface.h>
#include <mutex>

namespace fly {

class TcpConnectionManager : public ConnectionManager {
public:
    TcpConnectionManager();
    ~TcpConnectionManager() override;

    bool listen(const CMString& address, int port) override;
    void stop_listening() override;
    uint64_t connect(const CMString& address, int port) override;
    ssize_t send(uint64_t conn_id, const CMString& data) override;
    CMVector<TransportEvent> poll(int timeout_ms) override;
    void close(uint64_t conn_id) override;
    void close_all() override;
    bool is_connected(uint64_t conn_id) const override;
    size_t connection_count() const override;
    int get_bound_port() const override;

private:
    CMSharedPtr<Transport> transport_;
    CMSharedPtr<EpollMultiplexer> epoll_;
    int epoll_fd_ = -1;
    int listen_fd_ = -1;
    uint64_t next_conn_id_ = 1;

    mutable std::mutex conn_mutex_;
    CMUnorderedMap<uint64_t, int> conn_to_fd_;
    CMUnorderedMap<int, uint64_t> fd_to_conn_;

    CMUnorderedMap<uint64_t, CMString> write_buffers_;

#ifdef FLY_ENABLE_TEST_HOOKS
public:
    // 测试可见（仅 FLY_ENABLE_TEST_HOOKS 编译时）：register_connection / unregister_connection
    // 是 accept 路径的内部 helper，测试需直接调用以复现 fd 复用注册竞态（issue 007 Problem 6）。
    // release 不定义该宏 → 保持 private。访问控制是调用点侧检查：fly_network 库无宏编译时
    // 这些方法仍为 private，符号照常导出可链接。
#endif
    uint64_t register_connection(int fd);
    void unregister_connection(uint64_t conn_id);
private:
    CMString drain_socket(int fd, size_t max_size);
    void drain_write_buffer(uint64_t conn_id, int fd);
    void mod_epoll_events(int fd, uint32_t events);
    // epoll fd 惰性创建（listen/connect 首次使用时）：构造函数不做可失败的
    // 系统调用、不抛异常（issue 002：错误经 listen 的 bool / connect 的 0 哨兵返回）。
    bool ensure_epoll();
};

}  // namespace fly
