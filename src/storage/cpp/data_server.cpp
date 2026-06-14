#include <storage/cpp/data_server.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_utils.h>
#include <log/cpp/logger.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>
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

    running_ = true;

    io_threads_.reserve(thread_count_);
    for (int i = 0; i < thread_count_; ++i) {
        io_threads_.emplace_back(&DataServer::io_loop, this);
    }

    accept_thread_ = std::thread(&DataServer::accept_loop, this);

    INFO("DataServer listening on port {} with {} threads", data_port_, thread_count_);
}

void DataServer::stop() {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (!running_.exchange(false)) return;

    queue_cv_.notify_all();

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    for (auto& t : io_threads_) {
        if (t.joinable()) t.join();
    }
    io_threads_.clear();
}

void DataServer::accept_loop() {
    while (running_) {
        struct pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        int ret = ::poll(&pfd, 1, 10);
        if (ret <= 0) continue;

        int fd = ::accept4(listen_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (!running_) return;
            continue;
        }

        int nodelay = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending_fds_.push(fd);
        }
        queue_cv_.notify_one();
    }
}

void DataServer::io_loop() {
    while (running_) {
        int fd = -1;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !running_ || !pending_fds_.empty(); });
            if (!running_ && pending_fds_.empty()) return;
            if (!pending_fds_.empty()) {
                fd = pending_fds_.front();
                pending_fds_.pop();
            }
        }
        if (fd >= 0) {
            handle_connection(fd);
        }
    }
}

void DataServer::handle_connection(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    while (running_) {
        char header[5] = {};
        if (!net_recv_exact(fd, header, 5, 30000)) {
            break;
        }

        uint32_t total_len =
            (static_cast<uint32_t>(static_cast<unsigned char>(header[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(header[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(header[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(header[3]));

        if (total_len < 1) {
            break;
        }

        uint32_t payload_len = total_len - 1;
        CMString payload(payload_len, '\0');
        if (payload_len > 0 && !net_recv_exact(fd, payload.data(), payload_len, 30000)) {
            break;
        }

        CMString full_buf;
        full_buf.resize(4 + total_len);
        std::memcpy(&full_buf[0], header, 5);
        if (payload_len > 0) {
            std::memcpy(&full_buf[5], payload.data(), payload_len);
        }

        DataRequestMessage req;
        if (!MessageProtocol::decode(full_buf, req)) {
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
        if (!net_send_all(fd, resp_frame.data(), resp_frame.size(), 30000)) {
            break;
        }
    }

    ::close(fd);
}

}  // namespace fly
