#pragma once

#include <network/cpp/connection_manager.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <log/cpp/logger.h>
#include <functional>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>

namespace fly {

template<typename T>
using MessageHandler = std::function<void(uint64_t conn_id, const T& msg)>;

using GenericHandler = std::function<void(uint64_t conn_id, CMString& raw_msg)>;

// handler 执行池：两个执行域。
// - 通用 worker（submit）：无顺序要求任务的并行执行，有界队列满时拒绝。
// - 串行 lane（submit_to_lane）：每个 lane 一个专用线程 + FIFO 队列，同 lane
//   任务严格按提交序执行。用于消息 handler 分发——同一连接的消息哈希到同一
//   lane，保证同连接消息处理顺序（Register→*、WriteRegister→TaskComplete 等
//   协议顺序依赖），跨连接并行。lane 队列无界（控制面流量小，拒绝即丢消息，
//   不可接受）；shutdown 时先排空再退出线程。
class HandlerThreadPool {
public:
    explicit HandlerThreadPool(size_t num_threads, size_t max_queue_size = 100,
                               size_t num_lanes = 0);
    ~HandlerThreadPool();

    bool submit(std::function<void()> task);
    void submit_to_lane(size_t lane, std::function<void()> task);
    void wait_idle(int timeout_ms);
    size_t lane_count() const { return lanes_.size(); }
    void shutdown();
    bool is_shutdown() const { return stop_.load(); }

private:
    CMVector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    size_t max_queue_size_;

    struct Lane {
        std::thread worker;
        std::deque<std::function<void()>> tasks;
        std::mutex mutex;
        std::condition_variable cv;
        std::atomic<size_t> pending{0};  // 排队 + 执行中
    };
    CMVector<CMUniquePtr<Lane>> lanes_;

    void worker_loop();
    void lane_loop(size_t lane_idx);
};

class Reactor {
public:
    explicit Reactor(CMUniquePtr<ConnectionManager> transport, size_t handler_lanes = 0);
    ~Reactor();
    
    template<typename T>
    void register_handler(MessageHandler<T> handler);
    
    void on_connect(std::function<void(uint64_t)> handler);
    void on_disconnect(std::function<void(uint64_t)> handler);
    void on_error(std::function<void(uint64_t, int)> handler);
    
    void run();
    void run_once(int timeout_ms = 100);
    // 等待全部 lane 上的已提交 handler 执行完毕（run 循环退出后调用，此后无新
    // 提交）。必须在外部销毁 Reactor 前调用：~Reactor 内的池排空虽会 join lane
    // 线程，但此刻持有者（agent）的 unique_ptr 已置 null，迟到的 handler 经
    // agent 成员访问 reactor_ 会解引用空指针。
    void drain_handlers(int timeout_ms = 10000);
    void stop();
    void wait_until_running() const;
    bool is_running() const { return running_.load(); }
    
    int get_bound_port() const { return transport_->get_bound_port(); }

    // 顺序敏感域（serialized domain）：注册进本域的消息类型与（可选）连接
    // 生命周期事件回调（connect/disconnect/error）不参与 conn 分 lane——统一
    // 投递到保留串行 lane，跨连接严格按提交序 FIFO 执行。
    //
    // 背景（P3-26）：lane 分发只保证同连接保序；身份生命周期等协议
    // （REGISTER vs 旧 conn DISCONNECT vs WorkerProbeAck）依赖跨连接的处理
    // 顺序——跨 lane 并行交错会产生 check-act 窗口（deferred 注册孤儿化 →
    // worker 挂死被误拒）。后续新增「必须全局串行」的消息一律加入本域。
    //
    // 代价：域内消息与同连接其他消息不再保序——加入的类型必须与同连接其他
    // 消息顺序无关（自包含协议消息）。lifecycle_events=true 时全部连接事件
    // 回调入域（事件低频，可接受）。
    //
    // 约束：必须在 run() 之前调用（run 循环启动后集合只读）。
    void set_serialized_domain(CMVector<MessageType> types, bool lifecycle_events);

    // conn 是否仍在 transport 连接表（fd 未被 reap）。供「向旧 conn 发探测
    // 前后判断其存活性」等决策使用。
    bool is_connected(uint64_t conn_id) const { return transport_->is_connected(conn_id); }

    template<typename T>
    bool send(uint64_t conn_id, const T& msg);

    template<typename T>
    bool try_send(uint64_t conn_id, const T& msg);
    
    uint64_t connect(const CMString& host, int port) {
        return transport_->connect(host, port);
    }

    // 主动关闭连接（transport 层 close）：对端与本地的 reactor 都将收到断连
    // 事件。测试模拟真实 TCP 闪断用（只触发回调不关 fd 的模拟会让对端连接
    // 表残留，撞上先到先得的重复注册判定）。
    void close_connection(uint64_t conn_id) {
        transport_->close(conn_id);
    }

private:
    CMUniquePtr<ConnectionManager> transport_;
    
    CMUnorderedMap<uint64_t, CMString> recv_buffers_;
    
    CMUnorderedMap<MessageType, GenericHandler> handlers_;
    
    std::function<void(uint64_t)> connect_handler_;
    std::function<void(uint64_t)> disconnect_handler_;
    std::function<void(uint64_t, int)> error_handler_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    // handler 并行执行域。handler_lanes == 0 时为 nullptr，所有 handler 在
    // reactor 线程内联执行（legacy 行为）。> 0 时：帧提取留在 reactor 线程
    // （recv_buffers_ 只被 reactor 线程触碰），decode + handler 执行投递到
    // conn_id % lanes 的专用 lane——同连接消息严格保序，跨连接并行。
    CMUniquePtr<HandlerThreadPool> handler_pool_;

    // per-conn send mutex 用 shared_ptr 持有：disconnect（reactor 线程）erase 条目
    // 时，其他线程（lane handler/heartbeat）已取出的引用继续保活，杜绝「mutex 被
    // 持有时析构」的 use-after-free（lane 并行化后该窗口被显著放大）。
    std::mutex conn_send_mutex_map_mutex_;
    CMUnorderedMap<uint64_t, CMSharedPtr<std::mutex>> conn_send_mutexes_;
    CMSharedPtr<std::mutex> acquire_send_mutex(uint64_t conn_id);
    void remove_send_mutex(uint64_t conn_id);

    // 顺序敏感域（set_serialized_domain 注册）：命中类型/事件投递到保留串行
    // lane（下标 = handler_lane_count_），其余按 conn 分 lane。
    CMUnorderedSet<MessageType> serialized_types_;
    bool serialize_lifecycle_events_ = false;
    size_t handler_lane_count_ = 0;  // 常规 lane 数（不含保留串行 lane）

    void handle_event(const TransportEvent& event);
    void dispatch_message(uint64_t conn_id, CMString& buffer);
    // 事件回调（connect/disconnect/error）经该 conn 的 lane 执行，保证与该
    // conn 在途消息的先后关系（如 on_disconnect 必须晚于已提交的 handler）。
    // 顺序敏感域开启 lifecycle_events 时改投保留串行 lane（跨连接 FIFO）。
    void run_event_callback(uint64_t conn_id, std::function<void()> cb);
};

template<typename T>
void Reactor::register_handler(MessageHandler<T> handler) {
    handlers_[T::msg_type_] = [handler](uint64_t conn_id, CMString& raw) {
        T msg;
        if (MessageProtocol::decode(raw, msg)) {
            handler(conn_id, msg);
        }
    };
}

template<typename T>
bool Reactor::send(uint64_t conn_id, const T& msg) {
    CMString frame = MessageProtocol::encode(msg);
    auto mtx = acquire_send_mutex(conn_id);
    if (!mtx) return false;  // 已断开（条目被 erase 且无缓存）
    std::lock_guard<std::mutex> lock(*mtx);
    ssize_t result = transport_->send(conn_id, frame);
    if (result < 0) {
        WARN("Reactor::send failed for conn_id={}", conn_id);
        return false;
    }
    return true;
}

template<typename T>
bool Reactor::try_send(uint64_t conn_id, const T& msg) {
    CMString frame = MessageProtocol::encode(msg);
    auto mtx = acquire_send_mutex(conn_id);
    if (!mtx) return false;
    if (!mtx->try_lock()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(*mtx, std::adopt_lock);
    ssize_t result = transport_->send(conn_id, frame);
    if (result < 0) {
        WARN("Reactor::try_send failed for conn_id={}", conn_id);
        return false;
    }
    return true;
}

}  // namespace fly
