#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <sys/epoll.h>

namespace fly {

class DataService;

/**
 * DataServer: epoll-based network server for DataService.
 *
 * Architecture:
 *   - N epoll threads: accept connections, recv requests, decode, submit responses to send_queue
 *   - M send threads:  drain send_queue, send responses on fds
 *
 * Key design rules (derived from previous debugging):
 *   1. ALL responses go through send_queue — no direct_send in epoll thread
 *   2. on_readable ALWAYS rearms fd after processing (unless fd is closed)
 *   3. send_thread NEVER touches epoll (no rearm)
 *   4. Client protocol is strictly sequential per connection (request → response → next request),
 *      so no concurrent send on same fd is possible.
 *
 * Connection lifecycle:
 *   accept → epoll ADD (EPOLLIN|EPOLLONESHOT) → epoll_wait → on_readable
 *   on_readable: recv → decode → try_read → encode → push send_queue → rearm
 *   send_thread: pop send_queue → send all bytes → done
 *   client closes → epoll fires → on_readable: recv=0 → close fd
 */
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
    std::thread accept_thread_;
    std::mutex start_mutex_;

    // ── Per-connection state ──
    struct ConnState {
        int fd = -1;
        CMString recv_buf;  // partial frame accumulator
    };
    std::mutex conn_mutex_;
    CMVector<ConnState> conns_;

    // ── Epoll threads ──
    CMVector<std::thread> epoll_threads_;

    // ── Send queue ──
    struct SendTask {
        int fd = -1;
        CMString data;
    };
    std::queue<SendTask> send_queue_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    CMVector<std::thread> send_threads_;
    std::atomic<int> send_pending_{0};  // per-fd pending send count (debug)

    // ── Methods ──
    void accept_loop();
    void epoll_loop();
    void on_readable(int fd);
    void send_loop();
    void do_send(int fd, const CMString& data);
    void cleanup_fd(int fd);
    int find_conn_index(int fd);
};

}  // namespace fly
