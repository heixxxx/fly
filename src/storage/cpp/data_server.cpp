#include <storage/cpp/data_server.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_utils.h>
#include <log/cpp/logger.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>

namespace fly {

DataServer::DataServer(DataService& ds, int thread_count)
    : data_service_(ds), thread_count_(thread_count) {}

DataServer::~DataServer() {
    stop();
}

void DataServer::start(const CMString& host, int port) {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (running_) return;

    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        ERR("DataServer: failed to create listen socket: {}", std::strerror(errno));
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ERR("DataServer: bind failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, 128) < 0) {
        ERR("DataServer: listen failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len);
    data_port_ = ntohs(bound_addr.sin_port);

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        ERR("DataServer: epoll_create1 failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev);

    running_ = true;

    threads_.reserve(thread_count_);
    for (int i = 0; i < thread_count_; ++i) {
        threads_.emplace_back(&DataServer::epoll_loop, this);
    }

    INFO("DataServer listening on port {} with {} epoll threads", data_port_, thread_count_);
}

void DataServer::stop() {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (!running_.exchange(false)) return;

    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();

    std::lock_guard<std::mutex> cl(conn_mutex_);
    for (auto& [fd, cs] : conns_) {
        ::close(fd);
    }
    conns_.clear();
}

void DataServer::epoll_loop() {
    struct epoll_event events[64];

    while (running_) {
        int n = epoll_wait(epoll_fd_, events, 64, 10);
        if (n <= 0) continue;

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_fd_) {
                while (true) {
                    int client_fd = ::accept4(listen_fd_, nullptr, nullptr,
                                              SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (client_fd < 0) break;

                    int nodelay = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                    struct epoll_event cev;
                    cev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
                    cev.data.fd = client_fd;
                    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &cev);

                    std::lock_guard<std::mutex> lk(conn_mutex_);
                    conns_[client_fd] = ConnState{};
                }
                continue;
            }

            if (events[i].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
                cleanup_fd(fd);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                on_data(fd);
            }
        }
    }
}

void DataServer::on_data(int fd) {
    char buf[65536];
    CMString accumulated;

    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            accumulated.append(buf, n);
            continue;
        }
        if (n == 0) {
            cleanup_fd(fd);
            return;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        cleanup_fd(fd);
        return;
    }

    if (accumulated.empty()) return;

    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        auto it = conns_.find(fd);
        if (it == conns_.end()) return;
        it->second.recv_buf += accumulated;
    }

    while (true) {
        CMString current;
        {
            std::lock_guard<std::mutex> lk(conn_mutex_);
            auto it = conns_.find(fd);
            if (it == conns_.end()) return;
            current = it->second.recv_buf;
        }

        if (current.size() < 5) break;

        uint32_t total_len =
            (static_cast<uint32_t>(static_cast<unsigned char>(current[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(current[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(current[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(current[3]));

        if (total_len < 1) break;
        uint32_t frame_size = 4 + total_len;
        if (current.size() < frame_size) break;

        {
            std::lock_guard<std::mutex> lk(conn_mutex_);
            auto it = conns_.find(fd);
            if (it == conns_.end()) return;
            it->second.recv_buf.erase(0, frame_size);
        }

        process_frame(fd, current.substr(0, frame_size));
    }
}

void DataServer::process_frame(int fd, const CMString& frame) {
    DataRequestMessage req;
    if (!MessageProtocol::decode(const_cast<CMString&>(frame), req)) {
        cleanup_fd(fd);
        return;
    }

    DBG("DataServer: DataRequest for object={}", req.object_name_);

    DataResponseMessage response;
    response.object_name_ = req.object_name_;

    auto [found, raw_data] = data_service_.try_read_local_raw(req.object_name_);

    if (found) {
        response.success_ = true;
        response.compressed_data_ = std::move(raw_data);

        DecompressingStreamBuf dsbuf(response.compressed_data_.data(),
                                     response.compressed_data_.size());
        response.py_name_ = dsbuf.py_name();

        auto write_hash = data_service_.get_write_context_hash(req.object_name_);
        if (!write_hash.empty()) {
            response.write_context_hash_ = write_hash;
        }
    } else {
        response.success_ = false;
        if (data_service_.is_write_in_progress(req.object_name_)) {
            response.error_message_ = "DATA_NOT_READY";
        } else {
            response.error_message_ = "OBJECT_NOT_FOUND";
        }
    }

    CMString resp_frame = MessageProtocol::encode(response);

    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    bool send_ok = net_send_all(fd, resp_frame.data(), resp_frame.size(), 30000);

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (send_ok) {
        struct epoll_event rev;
        rev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
        rev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &rev);
    } else {
        ERR("DataServer: failed to send response for {}", req.object_name_);
        cleanup_fd(fd);
    }
}

void DataServer::cleanup_fd(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    std::lock_guard<std::mutex> lk(conn_mutex_);
    conns_.erase(fd);
}

}  // namespace fly
