#pragma once

#include <common/cpp/common_types.h>
#include <network/cpp/epoll_multiplexer.h>
#include <common/cpp/fly_buffer.h>
#include <cstdint>
#include <functional>
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
        // L2 分片服务上下文（CHUNK_RESEND 路由用）：最近一次分片服务的对象
        // 区间 + 每字节区间重传记录（上限一次，§14.1 A'3）。
        CMString chunk_file;
        uint64_t chunk_off = 0;
        uint64_t chunk_size = 0;
        CMUnorderedSet<uint64_t> resent_offsets;
    };
    std::mutex conn_mutex_;
    CMVector<ConnState> conns_;

    // fd 代际号（conn_mutex_ 保护）：accept 递增分配；cleanup 递增失效——
    // 滞留发送任务凭入队时代际与当前代际比对，判定 fd 是否已被复用。
    std::unordered_map<int, uint64_t> fd_generations_;
    uint64_t fd_gen_counter_ = 0;
    uint64_t fd_generation_grab(int fd);   // 取当前代际（入队捕获用）
    void fd_generation_invalidate(int fd); // cleanup 时使旧代际失效

    CMVector<std::thread> epoll_threads_;

    struct SendTask {
        int fd = -1;
        CMString data;
        FlyBufferPtr raw_data;  // optional: raw payload sent after `data` (zero-copy)
        // L2 自含分片任务（§7.1 #20）：单个闭包在 send 线程执行 META → CHUNK
        // 流 → DIGEST 的完整发送（避免多任务乱序写同 fd）；raw_data/data 留空。
        std::function<void()> chunked_execute;
        // fd 代际号（入队时捕获）：连接清理后 fd 数值可能被新连接复用，滞留
        // 任务若不校验代际会把数据发给错误的连接。发送线程执行前比对，不一致
        // 即丢弃。
        uint64_t fd_generation = 0;
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

    // L2 分片路径（§4.5）。loc 参数退化为基本类型（避免与 data_service.h
    // 循环 include）。
    void serve_chunked(int fd, const CMString& object_name,
                       const CMString& file_path, uint64_t offset, uint64_t size);
    void handle_chunk_resend(int fd, uint64_t offset, uint64_t length);
};

}  // namespace fly
