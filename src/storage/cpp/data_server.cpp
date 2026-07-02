#include <storage/cpp/data_server.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/epoll_multiplexer.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <log/cpp/logger.h>
#include <cstring>
#include <cerrno>

#ifdef DBG
#undef DBG
#endif
#define DBG(...) ((void)0)

namespace fly {

DataServer::DataServer(DataService& ds, CMSharedPtr<Transport> transport,
                       CMSharedPtr<EpollMultiplexer> epoll, int thread_count)
    : data_service_(ds)
    , transport_(std::move(transport))
    , epoll_(std::move(epoll))
    , thread_count_(thread_count > 1 ? thread_count : 2) {}

DataServer::DataServer(DataService& ds, int thread_count)
    : DataServer(ds, create_tcp_transport(), create_epoll_multiplexer(), thread_count) {}

DataServer::~DataServer() {
    stop();
}

void DataServer::start(const CMString& host, int port) {
    std::lock_guard<std::mutex> lk(start_mutex_);
    if (running_) return;

    listen_fd_ = transport_->create_listen_socket(host, port);
    if (listen_fd_ < 0) {
        ERR("DataServer: create_listen_socket() failed");
        return;
    }

    data_port_ = transport_->get_port(listen_fd_);

    epoll_fd_ = epoll_->create();
    if (epoll_fd_ < 0) {
        ERR("DataServer: epoll create() failed");
        transport_->close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    if (!epoll_->add(epoll_fd_, listen_fd_, EV_READ)) {
        ERR("DataServer: epoll add(listen_fd) failed");
        epoll_->destroy(epoll_fd_);
        transport_->close(listen_fd_);
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
        transport_->close(listen_fd_);
        listen_fd_ = -1;
    }

    if (epoll_fd_ >= 0) {
        epoll_->destroy(epoll_fd_);
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
            transport_->close(c.fd);
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
    epoll_->del(epoll_fd_, fd);
    {
        std::lock_guard<std::mutex> lk(conn_mutex_);
        int idx = find_conn_index(fd);
        if (idx >= 0) {
            conns_[idx] = std::move(conns_.back());
            conns_.pop_back();
        }
    }
    transport_->close(fd);
}

void DataServer::epoll_loop() {
    while (running_) {
        IoEvent events[64];
        int n = epoll_->wait(epoll_fd_, events, 64, 100);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (!running_) return;
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].fd;

            if (fd == listen_fd_) {
                while (true) {
                    int cfd = transport_->accept_connection(listen_fd_);
                    if (cfd < 0) break;

                    transport_->set_nonblocking(cfd);

                    {
                        std::lock_guard<std::mutex> lk(conn_mutex_);
                        conns_.push_back({cfd, {}});
                    }

                    epoll_->add(epoll_fd_, cfd, EV_READ | EV_ONESHOT);

                    DBG("[DS-ACCEPT] fd={} total={}", cfd, conns_.size());
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
        ssize_t n = transport_->recv(fd, tmp, sizeof(tmp));
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
        uint32_t total_len = read_be32(buf);

        if (total_len < 1) {
            ERR("[DS-FRAME] fd={} invalid total_len=0", fd);
            break;
        }

        uint32_t frame_size = 4 + total_len;
        if (buf.size() < frame_size) break;

        CMString frame(buf.data(), frame_size);
        buf.erase(0, frame_size);

        // Dispatch by message type. The data plane now carries both read
        // requests and bandwidth probes; the type byte routes each frame.
        MessageType mtype = MessageProtocol::get_type(frame);

        if (mtype == MessageType::NET_PROBE_REQUEST) {
            NetProbeRequestMessage preq;
            if (!MessageProtocol::decode(frame, preq)) {
                ERR("[DS-DECODE] fd={} probe decode failed", fd);
                break;
            }
            NetProbeResponseMessage presp;
            presp.probe_seq_ = preq.probe_seq_;
            presp.payload_.assign(preq.payload_size_, 0);
            CMString encoded = MessageProtocol::encode(presp);
            {
                std::lock_guard<std::mutex> slk(send_mutex_);
                SendTask task;
                task.fd = fd;
                task.data = std::move(encoded);
                send_queue_.push(std::move(task));
            }
            send_cv_.notify_one();
            pushed_response = true;
            continue;
        }

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
            // Parse py_name directly from the FlyBuffer (zero-copy via string_view).
            DecompressingStreamBuf dsbuf(raw_data->data(), raw_data->size());
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

        // Two-segment encode: small fields via bitsery, raw payload referenced
        // by pointer (zero-copy — raw_data FlyBufferPtr shared ownership).
        auto seg = DataResponseProtocol::encode(response, found ? raw_data : nullptr);

        {
            std::lock_guard<std::mutex> slk(send_mutex_);
            SendTask task;
            task.fd = fd;
            task.data = std::move(seg.header_segment);
            task.raw_data = found ? raw_data : nullptr;  // keep alive for send thread
            send_queue_.push(std::move(task));
            DBG("[DS-Q] fd={} pushed queue_size={}", fd, send_queue_.size());
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
        epoll_->mod(epoll_fd_, fd, EV_READ | EV_ONESHOT);
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
            bool has_raw = task.raw_data && !task.raw_data->empty();
            if (has_raw) {
                // Single writev call for header + raw payload (avoids 2x send overhead).
                struct iovec iov[2];
                iov[0].iov_base = const_cast<char*>(task.data.data());
                iov[0].iov_len = task.data.size();
                iov[1].iov_base = const_cast<char*>(task.raw_data->data());
                iov[1].iov_len = task.raw_data->size();
                bool ok = transport_->sendv(task.fd, iov, 2);
                if (!ok) {
                    ERR("[DS-SEND] sendv failed: fd={}", task.fd);
                    cleanup_fd(task.fd);
                } else {
                    DBG("[DS-SEND] fd={} complete: {} + {} bytes (sendv)", task.fd, task.data.size(), task.raw_data->size());
                    epoll_->mod(epoll_fd_, task.fd, EV_READ | EV_ONESHOT);
                }
            } else {
                do_send(task.fd, task.data);
            }
        }
    }
}

void DataServer::do_send(int fd, const CMString& data) {
    bool ok = transport_->send_all(fd, data.data(), data.size());

    if (!ok) {
        ERR("[DS-SEND] fd={} failed: {}/{} bytes", fd, 0, data.size());
        cleanup_fd(fd);
    } else {
        DBG("[DS-SEND] fd={} complete: {} bytes", fd, data.size());
        epoll_->mod(epoll_fd_, fd, EV_READ | EV_ONESHOT);
    }
}

}  // namespace fly
