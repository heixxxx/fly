#include "tcp_transport.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <errno.h>

namespace fly {

TCPTransport::TCPTransport() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("Failed to create epoll: " + std::string(std::strerror(errno)));
    }
}

TCPTransport::~TCPTransport() {
    close_all();
    stop_listening();
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

void TCPTransport::listen(const CMString& address, int port) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error("Failed to create socket: " + std::string(std::strerror(errno)));
    }
    
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(address.c_str());
    addr.sin_port = htons(port);
    
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("Failed to bind to " + address + ":" + std::to_string(port) + 
                                 ": " + std::string(std::strerror(errno)));
    }
    
    if (::listen(listen_fd_, 128) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("Failed to listen on port " + std::to_string(port) + 
                                 ": " + std::string(std::strerror(errno)));
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error("Failed to add listen socket to epoll: " + std::string(std::strerror(errno)));
    }
}

void TCPTransport::stop_listening() {
    if (listen_fd_ >= 0) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, listen_fd_, nullptr);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

uint64_t TCPTransport::connect(const CMString& address, int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        throw std::runtime_error("Failed to create client socket: " + std::string(std::strerror(errno)));
    }
    
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(address.c_str());
    addr.sin_port = htons(port);
    
    int ret = ::connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        ::close(fd);
        throw std::runtime_error("Failed to connect to " + address + ":" + std::to_string(port) + 
                                 ": " + std::string(std::strerror(errno)));
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        ::close(fd);
        throw std::runtime_error("Failed to add client socket to epoll: " + std::string(std::strerror(errno)));
    }
    
    return register_connection(fd);
}

ssize_t TCPTransport::send(uint64_t conn_id, const CMString& data) {
    auto it = conn_to_fd_.find(conn_id);
    if (it == conn_to_fd_.end() || it->second < 0) {
        return -1;
    }
    
    int fd = it->second;
    ssize_t sent = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    
    return sent;
}

ssize_t TCPTransport::recv(uint64_t conn_id, CMString& buffer, size_t max_size) {
    auto it = conn_to_fd_.find(conn_id);
    if (it == conn_to_fd_.end() || it->second < 0) {
        return -1;
    }
    
    int fd = it->second;
    buffer.resize(max_size);
    
    ssize_t received = ::recv(fd, buffer.data(), max_size, 0);
    
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            buffer.clear();
            return 0;
        }
        buffer.clear();
        return -1;
    }
    
    if (received == 0) {
        buffer.clear();
        return -1;
    }
    
    buffer.resize(received);
    return received;
}

CMVector<TransportEvent> TCPTransport::poll(int timeout_ms) {
    CMVector<TransportEvent> events;
    
    struct epoll_event evs[64];
    int n = epoll_wait(epoll_fd_, evs, 64, timeout_ms);
    
    if (n <= 0) {
        return events;
    }
    
    for (int i = 0; i < n; i++) {
        int fd = evs[i].data.fd;
        
        if (fd == listen_fd_) {
            int client_fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                continue;
            }
            
            struct epoll_event client_ev;
            client_ev.events = EPOLLIN | EPOLLET;
            client_ev.data.fd = client_fd;
            if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                ::close(client_fd);
                continue;
            }
            
            uint64_t new_conn_id = register_connection(client_fd);
            
            TransportEvent ev;
            ev.type = TransportEventType::CONNECT;
            ev.conn_id = new_conn_id;
            events.push_back(ev);
            
        } else {
            auto it = fd_to_conn_.find(fd);
            if (it == fd_to_conn_.end()) {
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                ::close(fd);
                continue;
            }
            
            uint64_t conn_id = it->second;
            
            if (evs[i].events & (EPOLLERR | EPOLLHUP)) {
                int error = 0;
                socklen_t len = sizeof(error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
                
                TransportEvent ev;
                ev.type = TransportEventType::ERROR;
                ev.conn_id = conn_id;
                ev.error_code = error;
                events.push_back(ev);
                
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                ::close(fd);
                unregister_connection(conn_id);
                continue;
            }
            
            if (evs[i].events & EPOLLIN) {
                CMString data = drain_socket(fd, 65536);
                
                if (data.empty()) {
                    TransportEvent ev;
                    ev.type = TransportEventType::DISCONNECT;
                    ev.conn_id = conn_id;
                    events.push_back(ev);
                    
                    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
                    ::close(fd);
                    unregister_connection(conn_id);
                } else {
                    TransportEvent ev;
                    ev.type = TransportEventType::DATA;
                    ev.conn_id = conn_id;
                    ev.data = std::move(data);
                    events.push_back(ev);
                }
            }
        }
    }
    
    return events;
}

void TCPTransport::close(uint64_t conn_id) {
    auto it = conn_to_fd_.find(conn_id);
    if (it != conn_to_fd_.end() && it->second >= 0) {
        int fd = it->second;
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        unregister_connection(conn_id);
    }
}

void TCPTransport::close_all() {
    for (const auto& [conn_id, fd] : conn_to_fd_) {
        if (fd >= 0) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
            ::close(fd);
        }
    }
    conn_to_fd_.clear();
    fd_to_conn_.clear();
}

bool TCPTransport::is_connected(uint64_t conn_id) const {
    auto it = conn_to_fd_.find(conn_id);
    return it != conn_to_fd_.end() && it->second >= 0;
}

size_t TCPTransport::connection_count() const {
    return conn_to_fd_.size();
}

int TCPTransport::get_bound_port() const {
    if (listen_fd_ < 0) return -1;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
        return -1;
    }
    return ntohs(addr.sin_port);
}

uint64_t TCPTransport::register_connection(int fd) {
    uint64_t conn_id = next_conn_id_++;
    conn_to_fd_[conn_id] = fd;
    fd_to_conn_[fd] = conn_id;
    return conn_id;
}

void TCPTransport::unregister_connection(uint64_t conn_id) {
    auto it = conn_to_fd_.find(conn_id);
    if (it != conn_to_fd_.end()) {
        fd_to_conn_.erase(it->second);
        conn_to_fd_.erase(it);
    }
}

void TCPTransport::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

CMString TCPTransport::drain_socket(int fd, size_t max_size) {
    CMString buffer;
    buffer.resize(max_size);
    
    size_t total = 0;
    while (total < max_size) {
        ssize_t n = ::recv(fd, buffer.data() + total, max_size - total, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            buffer.clear();
            return buffer;
        }
        if (n == 0) {
            buffer.clear();
            return buffer;
        }
        total += n;
    }
    
    buffer.resize(total);
    return buffer;
}

CMUniquePtr<TransportLayer> create_transport(const CMString& type) {
    if (type == "tcp") {
        return CMMakeUnique<TCPTransport>();
    }
    throw std::runtime_error("Unknown transport type: " + type);
}

}  // namespace fly