#pragma once

#include <common/cpp/common_types.h>
#include <common/cpp/fd_handle.h>
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

    // 监听/epoll 实例描述符：跨线程读写（stop 关闭 vs epoll/send 线程在飞
    // 读），atomic 消除数据竞争；close 时序由 stop()「先 join 线程后关闭」
    // 保证（issue 011 M0）。
    std::atomic<int> listen_fd_{-1};
    std::atomic<int> epoll_fd_{-1};
    int data_port_ = 0;

    std::atomic<bool> running_{false};
    std::mutex start_mutex_;

    struct ConnState {
        FdHandlePtr handle;   // 连接身份（指针唯一）：在途使用者持引用保活
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

    CMVector<std::thread> epoll_threads_;

    struct SendTask {
        // 连接句柄（入队时快照）：发送全程持引用保活——连接被清理只
        // shutdown（对端立即收 FIN），滞留任务的后续写得到 EPIPE 被丢弃，
        // 绝不落到复用编号的新连接（issue 011，替代原 fd 代际校验机制）。
        FdHandlePtr handle;
        CMString data;
        FlyBufferPtr raw_data;  // optional: raw payload sent after `data` (zero-copy)
        // L2 自含分片任务（§7.1 #20）：单个闭包在 send 线程执行 META → CHUNK
        // 流 → DIGEST 的完整发送（避免多任务乱序写同 fd）；raw_data/data 留空。
        std::function<void()> chunked_execute;
    };
    std::queue<SendTask> send_queue_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    CMVector<std::thread> send_threads_;

    void epoll_loop();
    void on_readable(const FdHandlePtr& handle);
    void send_loop();
    void do_send(const FdHandlePtr& handle, const CMString& data);
    // 连接清理（幂等）：epoll 摘除 + 摘表 + 决策层 shutdown——close 由最后
    // 引用释放执行（滞留发送任务得 EPIPE，不写复用编号）。
    void cleanup_fd(const FdHandlePtr& handle);
    int find_conn_index(int fd);

    // L2 分片路径（§4.5）。loc 参数退化为基本类型（避免与 data_service.h
    // 循环 include）。
    void serve_chunked(const FdHandlePtr& handle, const CMString& object_name,
                       const CMString& file_path, uint64_t offset, uint64_t size);
    void handle_chunk_resend(const FdHandlePtr& handle, uint64_t offset, uint64_t length);
};

}  // namespace fly
