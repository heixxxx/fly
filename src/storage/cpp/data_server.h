#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

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
    int data_port_ = 0;

    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::mutex start_mutex_;

    std::queue<int> pending_fds_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    CMVector<std::thread> io_threads_;

    void accept_loop();
    void io_loop();
    void handle_connection(int fd);
};

}  // namespace fly
