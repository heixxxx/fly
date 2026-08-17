#include "tcp_connection_manager.h"
#include <network/cpp/tcp_socket.h>
#include <log/cpp/logger.h>
#include <sys/socket.h>
#include <cstring>
#include <cerrno>

namespace fly {

TcpConnectionManager::TcpConnectionManager() {
    transport_ = create_tcp_transport();
    epoll_ = create_epoll_multiplexer();
}

TcpConnectionManager::~TcpConnectionManager() {
    close_all();
    stop_listening();
    if (epoll_fd_ >= 0) {
        epoll_->destroy(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool TcpConnectionManager::ensure_epoll() {
    if (epoll_fd_ >= 0) return true;
    epoll_fd_ = epoll_->create();
    if (epoll_fd_ < 0) {
        ERR("Failed to create epoll");
        return false;
    }
    return true;
}

bool TcpConnectionManager::listen(const CMString& address, int port) {
    if (!ensure_epoll()) return false;
    listen_fd_ = transport_->create_listen_socket(address, port);
    if (listen_fd_ < 0) {
        ERR("Failed to create listen socket for {}:{}", address, port);
        return false;
    }

    if (!epoll_->add(epoll_fd_, listen_fd_, EV_READ)) {
        ERR("Failed to add listen socket to epoll for {}:{}", address, port);
        transport_->close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    return true;
}

void TcpConnectionManager::stop_listening() {
    if (listen_fd_ >= 0) {
        epoll_->del(epoll_fd_, listen_fd_);
        transport_->close(listen_fd_);
        listen_fd_ = -1;
    }
}

uint64_t TcpConnectionManager::connect(const CMString& address, int port) {
    if (!ensure_epoll()) {
        WARN("epoll unavailable, connect to {}:{} failed", address, port);
        return 0;
    }
    int fd = transport_->create_connection(address, port);
    if (fd < 0) {
        WARN("connect failed to {}:{}", address, port);
        return 0;  // failure sentinel (valid conn_id starts at 1)
    }

    transport_->set_nonblocking(fd);

    if (!epoll_->add(epoll_fd_, fd, EV_READ)) {
        transport_->close(fd);
        WARN("epoll add failed for connect to {}:{}", address, port);
        return 0;
    }

    return register_connection(fd);
}

void TcpConnectionManager::mod_epoll_events(int fd, uint32_t events) {
    epoll_->mod(epoll_fd_, fd, events);
}

ssize_t TcpConnectionManager::send(uint64_t conn_id, const CMString& data) {
    int fd;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        auto it = conn_to_fd_.find(conn_id);
        if (it == conn_to_fd_.end() || it->second < 0) {
            DBG("[TCP-SEND] unknown conn_id={}", conn_id);
            return -1;
        }
        fd = it->second;

        auto wbuf_it = write_buffers_.find(conn_id);
        if (wbuf_it != write_buffers_.end() && !wbuf_it->second.empty()) {
            wbuf_it->second.append(data);
            // 防御：确保 EV_WRITE 已注册（drain 清空 buffer 后会移除 EV_WRITE；
            // 若此刻新数据 append 进来而 EV_WRITE 未注册，buffer 永远不会 drain，
            // 导致消息丢失 → master/worker 永远等不到该消息 → 调度/退出卡死）。
            mod_epoll_events(fd, EV_READ | EV_WRITE);
            return static_cast<ssize_t>(data.size());
        }
    }

    ssize_t sent = transport_->send(fd, data.data(), data.size());

    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            // append 而非覆盖：drain_write_buffer 可能在 send 释放 conn_mutex_ 的窗口里
            // 部分消费了 write_buffers_，直接赋值会丢掉剩余数据。
            write_buffers_[conn_id].append(data);
            mod_epoll_events(fd, EV_READ | EV_WRITE);
            return static_cast<ssize_t>(data.size());
        }
        ERR("[TCP-SEND] error conn_id={} fd={} errno={}", conn_id, fd, errno);
        return -1;
    }

    if (static_cast<size_t>(sent) < data.size()) {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        // append 剩余数据，不覆盖 drain_write_buffer 可能残留的数据
        write_buffers_[conn_id].append(data.data() + sent, data.size() - sent);
        mod_epoll_events(fd, EV_READ | EV_WRITE);
    }

    return static_cast<ssize_t>(data.size());
}

void TcpConnectionManager::drain_write_buffer(uint64_t conn_id, int fd) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = write_buffers_.find(conn_id);
    if (it == write_buffers_.end() || it->second.empty()) {
        mod_epoll_events(fd, EV_READ);
        return;
    }

    CMString& buf = it->second;
    size_t total_sent = 0;
    while (total_sent < buf.size()) {
        ssize_t sent = transport_->send(fd, buf.data() + total_sent, buf.size() - total_sent);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            buf.clear();
            write_buffers_.erase(it);
            return;
        }
        if (sent == 0) break;
        total_sent += static_cast<size_t>(sent);
    }

    if (total_sent > 0) {
        buf.erase(0, total_sent);
    }

    if (buf.empty()) {
        write_buffers_.erase(it);
        mod_epoll_events(fd, EV_READ);
    } else {
        // drain 后仍有残留：socket 持续不可写（消费端不读）。
        // 大残留长时间不消 = 消息积压风险，WARN 落盘便于定位。
        if (buf.size() > 1048576) {  // 1MB — 异常积压阈值
            WARN("[WBUF] drain-leftover conn={} fd={} remaining={}",
                 conn_id, fd, buf.size());
        }
    }
}

CMVector<TransportEvent> TcpConnectionManager::poll(int timeout_ms) {
    CMVector<TransportEvent> events;

    IoEvent evs[64];
    int n = epoll_->wait(epoll_fd_, evs, 64, timeout_ms);

    if (n <= 0) {
        return events;
    }

    for (int i = 0; i < n; i++) {
        int fd = evs[i].fd;

        if (fd == listen_fd_) {
            int client_fd = transport_->accept_connection(listen_fd_);
            if (client_fd < 0) {
                continue;
            }

            transport_->set_nonblocking(client_fd);

            if (!epoll_->add(epoll_fd_, client_fd, EV_READ)) {
                transport_->close(client_fd);
                continue;
            }

            uint64_t new_conn_id = register_connection(client_fd);

            TransportEvent ev;
            ev.type_ = TransportEventType::CONNECT;
            ev.conn_id_ = new_conn_id;
            events.push_back(ev);

        } else {
            uint64_t conn_id;
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                auto it = fd_to_conn_.find(fd);
                if (it == fd_to_conn_.end()) {
                    epoll_->del(epoll_fd_, fd);
                    transport_->close(fd);
                    continue;
                }
                conn_id = it->second;
            }

            if (evs[i].error || evs[i].hangup) {
                int error = 0;
                socklen_t len = sizeof(error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);

                TransportEvent ev;
                ev.type_ = TransportEventType::ERROR;
                ev.conn_id_ = conn_id;
                ev.error_code_ = error;
                events.push_back(ev);

                DBG("[TCP-CLOSE] EPOLLERR/HUP conn_id={}, fd={}", conn_id, fd);
                epoll_->del(epoll_fd_, fd);
                unregister_connection(conn_id);
                transport_->close(fd);
                continue;
            }

            if (evs[i].writable) {
                drain_write_buffer(conn_id, fd);
            }

            if (evs[i].readable) {
                CMString data = drain_socket(fd, 65536);

                if (data.empty()) {
                    TransportEvent ev;
                    ev.type_ = TransportEventType::DISCONNECT;
                    ev.conn_id_ = conn_id;
                    events.push_back(ev);

                    DBG("[TCP-CLOSE] DISCONNECT conn_id={}, fd={}", conn_id, fd);
                    epoll_->del(epoll_fd_, fd);
                    unregister_connection(conn_id);
                    transport_->close(fd);
                } else {
                    TransportEvent ev;
                    ev.type_ = TransportEventType::DATA;
                    ev.conn_id_ = conn_id;
                    ev.data_ = std::move(data);
                    events.push_back(ev);
                }
            }
        }
    }

    return events;
}

void TcpConnectionManager::close(uint64_t conn_id) {
    int fd;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        auto it = conn_to_fd_.find(conn_id);
        if (it == conn_to_fd_.end() || it->second < 0) {
            return;
        }
        fd = it->second;
        fd_to_conn_.erase(fd);
        conn_to_fd_.erase(it);
        write_buffers_.erase(conn_id);
    }
    epoll_->del(epoll_fd_, fd);
    DBG("[TCP-CLOSE] explicit close conn_id={}, fd={}", conn_id, fd);
    transport_->close(fd);
}

void TcpConnectionManager::close_all() {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (const auto& [conn_id, fd] : conn_to_fd_) {
        if (fd >= 0) {
            epoll_->del(epoll_fd_, fd);
            transport_->close(fd);
        }
    }
    conn_to_fd_.clear();
    fd_to_conn_.clear();
    write_buffers_.clear();
}

bool TcpConnectionManager::is_connected(uint64_t conn_id) const {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = conn_to_fd_.find(conn_id);
    return it != conn_to_fd_.end() && it->second >= 0;
}

size_t TcpConnectionManager::connection_count() const {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    return conn_to_fd_.size();
}

int TcpConnectionManager::get_bound_port() const {
    if (listen_fd_ < 0) return -1;
    return transport_->get_port(listen_fd_);
}

uint64_t TcpConnectionManager::register_connection(int fd) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    // Problem 6：同 fd 二次注册（双 accept / OS fd 复用）会覆盖 fd_to_conn_[fd]，而旧
    // conn_id 的 conn_to_fd_ 条目仍残留 → 孤儿（永不被清理，connection_count 虚高，且
    // 旧 conn_id 的 send 会经 conn_to_fd_ 误投到被复用的新 fd）。覆盖前先清掉旧 conn 条目。
    auto existing = fd_to_conn_.find(fd);
    if (existing != fd_to_conn_.end()) {
        WARN("[TCP-DUP] register_connection: fd={} 已注册(旧 conn_id={})，先清理旧条目再重用",
             fd, existing->second);
        conn_to_fd_.erase(existing->second);
        write_buffers_.erase(existing->second);  // 同 unregister_connection 语义
    }
    uint64_t conn_id = next_conn_id_++;
    conn_to_fd_[conn_id] = fd;
    fd_to_conn_[fd] = conn_id;
    return conn_id;
}

void TcpConnectionManager::unregister_connection(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = conn_to_fd_.find(conn_id);
    if (it != conn_to_fd_.end()) {
        fd_to_conn_.erase(it->second);
        conn_to_fd_.erase(it);
    }
    write_buffers_.erase(conn_id);
}

CMString TcpConnectionManager::drain_socket(int fd, size_t max_size) {
    CMString buffer;
    buffer.resize(max_size);

    size_t total = 0;
    while (total < max_size) {
        ssize_t n = transport_->recv(fd, buffer.data() + total, max_size - total);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            buffer.clear();
            return buffer;
        }
        if (n == 0) {
            // EOF (FIN)。若已读到了数据，先返回数据（DATA 事件），
            // 让下一轮 poll 产生 DISCONNECT。直接 clear 会丢弃已读数据，
            // 导致"数据 + FIN 同时到达"时丢消息（如 BYE + close 的 FIN）。
            if (total > 0) break;
            buffer.clear();
            return buffer;
        }
        total += static_cast<size_t>(n);
    }

    buffer.resize(total);
    return buffer;
}

CMUniquePtr<ConnectionManager> create_connection_manager(const CMString& type) {
    if (type == "tcp") {
        return CMMakeUnique<TcpConnectionManager>();
    }
    ERR("Unknown connection manager type: {}", type);
    return nullptr;
}

}  // namespace fly
