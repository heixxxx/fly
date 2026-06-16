#pragma once

#include <common/cpp/common_types.h>
#include <network/cpp/epoll_multiplexer.h>
#include <serialization/cpp/fly_buffer.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace fly {

class DataService;
class Transport;

class DataServer {
public:
    DataServer(DataService& ds, CMSharedPtr<Transport> transport,
               CMSharedPtr<EpollMultiplexer> epoll, int thread_count);

    DataServer(DataService& ds, int thread_count);
    ~DataServer();

    void start(const CMString& host, int port);
    void stop();

    int get_port() const { return data_port_; }

private:
    DataService& data_service_;
    CMSharedPtr<Transport> transport_;
    CMSharedPtr<EpollMultiplexer> epoll_;
    int thread_count_;

    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    int data_port_ = 0;

    std::atomic<bool> running_{false};
    std::mutex start_mutex_;

    struct ConnState {
        int fd = -1;
        CMString recv_buf;
    };
    std::mutex conn_mutex_;
    CMVector<ConnState> conns_;

    CMVector<std::thread> epoll_threads_;

    struct SendTask {
        int fd = -1;
        CMString data;
        FlyBufferPtr raw_data;  // optional: raw payload sent after `data` (zero-copy)
    };
    std::queue<SendTask> send_queue_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    CMVector<std::thread> send_threads_;

    void epoll_loop();
    void on_readable(int fd);
    void send_loop();
    void do_send(int fd, const CMString& data);
    void cleanup_fd(int fd);
    int find_conn_index(int fd);
};

}  // namespace fly
