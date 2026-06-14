#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <unordered_map>

namespace fly {

class DataService;

class DataServer {
public:
    DataServer(DataService& ds, int thread_count);
    ~DataServer();

    void start(const CMString& host, int port);
    void stop();

    int get_port() const { return data_port_; }

private:
    DataService& data_service_;
    int thread_count_;

    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    int data_port_ = 0;

    std::atomic<bool> running_{false};
    CMVector<std::thread> threads_;
    std::mutex start_mutex_;

    struct ConnState {
        CMString recv_buf;
    };
    std::unordered_map<int, ConnState> conns_;
    std::mutex conn_mutex_;

    void epoll_loop();
    void on_data(int fd);
    void process_frame(int fd, const CMString& frame);
    void cleanup_fd(int fd);
};

}  // namespace fly
