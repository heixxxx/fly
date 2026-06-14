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
#include <poll.h>
#include <fcntl.h>
#include <cerrno>

// Disable verbose DataServer logging: 9 DBG calls per request × Logger::mutex_
// contention blocks the reactor thread (which also logs), causing 10x slowdown.
#ifdef DBG
#undef DBG
#endif
#define DBG(...) ((void)0)

namespace fly {

DataServer::DataServer(DataService& ds, int thread_count)
    : data_service_(ds), thread_count_(thread_count > 1 ? thread_count : 2) {}

DataServer::~DataServer() {
    stop();
}

void DataServer::start(const CMString& host, int port) {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (running_) return;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        ERR("DataServer: socket() failed: {}", std::strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ERR("DataServer: bind() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (::listen(listen_fd_, 128) < 0) {
        ERR("DataServer: listen() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    ::getsockname(listen_fd_, reinterpret_cast<struct sockaddr*>(&bound_addr), &bound_len);
    data_port_ = ntohs(bound_addr.sin_port);

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        ERR("DataServer: epoll_create1() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    struct epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        ERR("DataServer: epoll_ctl(ADD listen_fd) failed: {}", std::strerror(errno));
        ::close(epoll_fd_);
        ::close(listen_fd_);
        epoll_fd_ = -1;
        listen_fd_ = -1;
        return;
    }

    running_ = true;

    int n_epoll = std::max(1, thread_count_ / 2);
    int n_send = std::max(1, thread_count_ - n_epoll);

    INFO("DataServer starting: port={} epoll_threads={} send_threads={}",
         data_port_, n_epoll, n_send);

    epoll_threads_.reserve(n_epoll);
    for (int i = 0; i < n_epoll; ++i) {
        epoll_threads_.emplace_back(&DataServer::epoll_loop, this);
    }

    send_threads_.reserve(n_send);
    for (int i = 0; i < n_send; ++i) {
        send_threads_.emplace_back(&DataServer::send_loop, this);
    }
}

void DataServer::stop() {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (!running_.exchange(false)) return;

    send_cv_.notify_all();

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }

    for (auto& t : epoll_threads_) {
        if (t.joinable()) t.join();
    }
    epoll_threads_.clear();

    for (auto& t : send_threads_) {
        if (t.joinable()) t.join();
    }
    send_threads_.clear();

    std::lock_guard<std::mutex> clkk(conn_mutex_);
    for (auto& c : conns_) {
        if (c.fd >= 0) {
            ::close(c.fd);
            c.fd = -1;
        }
    }
    conns_.clear();
}

int DataServer::find_conn_index(int fd) {
    for (int i = 0; i < static_cast<int>(conns_.size()); ++i) {
        if (conns_[i].fd == fd) return i;
    }
    return -1;
}

void DataServer::cleanup_fd(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx >= 0) {
            conns_[idx] = std::move(conns_.back());
            conns_.pop_back();
        }
    }
    ::close(fd);
}

void DataServer::epoll_loop() {
    constexpr int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        int n = ::epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (!running_) return;
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == listen_fd_) {
                while (true) {
                    int cfd = ::accept4(listen_fd_, nullptr, nullptr,
                                        SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (cfd < 0) break;

                    int nodelay = 1;
                    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

                    {
                        std::lock_guard<std::mutex> lk(conn_mutex_);
                        conns_.push_back({cfd, {}});
                    }

                    struct epoll_event ev;
                    std::memset(&ev, 0, sizeof(ev));
                    ev.events = EPOLLIN | EPOLLONESHOT;
                    ev.data.fd = cfd;
                    ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cfd, &ev);

                    INFO("[DS-ACCEPT] fd={} total={}", cfd, conns_.size());
                }
            } else {
                on_readable(fd);
            }
        }
    }
}

void DataServer::on_readable(int fd) {
    char tmp[65536];
    CMString new_data;
    bool got_eof = false;

    while (true) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), MSG_DONTWAIT);
        if (n > 0) {
            new_data.append(tmp, n);
            if (n < static_cast<ssize_t>(sizeof(tmp))) break;
        } else if (n == 0) {
            got_eof = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            got_eof = true;
            break;
        }
    }

    if (got_eof && new_data.empty()) {
        cleanup_fd(fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx < 0) return;
        conns_[idx].recv_buf.append(new_data);
    }

    CMString buf;
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx < 0) return;
        buf = std::move(conns_[idx].recv_buf);
        conns_[idx].recv_buf.clear();
    }

    bool pushed_response = false;

    while (buf.size() >= 5) {
        uint32_t total_len =
            (static_cast<uint32_t>(static_cast<unsigned char>(buf[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buf[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buf[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(buf[3]));

        if (total_len < 1) {
            ERR("[DS-FRAME] fd={} invalid total_len=0", fd);
            break;
        }

        uint32_t frame_size = 4 + total_len;
        if (buf.size() < frame_size) break;

        CMString frame(buf.data(), frame_size);
        buf.erase(0, frame_size);

        DataRequestMessage req;
        if (!MessageProtocol::decode(frame, req)) {
            ERR("[DS-DECODE] fd={} decode failed", fd);
            break;
        }

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

        {
            std::lock_guard<std::mutex> slk(send_mutex_);
            send_queue_.push({fd, std::move(resp_frame)});
            INFO("[DS-Q] fd={} pushed queue_size={}", fd, send_queue_.size());
        }
        send_cv_.notify_one();
        pushed_response = true;
    }

    if (!buf.empty()) {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx >= 0) {
            conns_[idx].recv_buf = std::move(buf);
        }
    }

    if (pushed_response) {
        // send_thread will rearm after send completes
    } else if (got_eof) {
        cleanup_fd(fd);
    } else {
        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLONESHOT;
        ev.data.fd = fd;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void DataServer::send_loop() {
    while (running_) {
        SendTask task;
        {
            std::unique_lock<std::mutex> lk(send_mutex_);
            send_cv_.wait(lk, [this] { return !running_ || !send_queue_.empty(); });
            if (!running_ && send_queue_.empty()) return;
            if (!send_queue_.empty()) {
                task = std::move(send_queue_.front());
                send_queue_.pop();
            }
        }

        if (task.fd >= 0 && !task.data.empty()) {
            do_send(task.fd, task.data);
        }
    }
}

void DataServer::do_send(int fd, const CMString& data) {
    size_t total = data.size();
    size_t sent = 0;
    const char* buf = data.data();

    while (sent < total && running_) {
        ssize_t n = ::send(fd, buf + sent, total - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd;
                pfd.fd = fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                int pret = ::poll(&pfd, 1, 5000);
                if (pret <= 0) break;
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
                continue;
            }
            if (errno == EINTR) continue;
            break;
        }
    }

    if (sent < total) {
        ERR("[DS-SEND] fd={} INCOMPLETE: {}/{}", fd, sent, total);
        cleanup_fd(fd);
    } else {
        INFO("[DS-SEND] fd={} complete: {} bytes", fd, sent);
        struct epoll_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLONESHOT;
        ev.data.fd = fd;
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

}  // namespace fly
