#include <network/cpp/reactor.h>
#include <log/cpp/logger.h>
#include <algorithm>

namespace fly {

HandlerThreadPool::HandlerThreadPool(size_t num_threads, size_t max_queue_size,
                                     size_t num_lanes)
    : max_queue_size_(max_queue_size) {
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back(&HandlerThreadPool::worker_loop, this);
    }
    // 先把 lanes_ 全部建好（push_back 可能 realloc），再启动线程——lane_loop
    // 通过 lanes_[idx] 取队列，vector 在线程启动后不可再变。
    lanes_.reserve(num_lanes);
    for (size_t i = 0; i < num_lanes; ++i) {
        lanes_.push_back(CMMakeUnique<Lane>());
    }
    for (size_t i = 0; i < num_lanes; ++i) {
        lanes_[i]->worker = std::thread(&HandlerThreadPool::lane_loop, this, i);
    }
}

HandlerThreadPool::~HandlerThreadPool() {
    shutdown();
}

void HandlerThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return stop_.load() || !tasks_.empty(); });
            if (stop_.load() && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        task();
    }
}

void HandlerThreadPool::lane_loop(size_t lane_idx) {
    Lane& lane = *lanes_[lane_idx];
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(lane.mutex);
            lane.cv.wait(lock, [&] { return stop_.load() || !lane.tasks.empty(); });
            if (stop_.load() && lane.tasks.empty()) return;
            task = std::move(lane.tasks.front());
            lane.tasks.pop_front();
        }
        task();
        lane.pending.fetch_sub(1, std::memory_order_relaxed);
    }
}

bool HandlerThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return false;
        if (tasks_.size() >= max_queue_size_) return false;
        tasks_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

void HandlerThreadPool::submit_to_lane(size_t lane, std::function<void()> task) {
    Lane& l = *lanes_[lane % lanes_.size()];
    l.pending.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(l.mutex);
        // 无界队列：控制面消息不允许丢弃（丢一条 TaskComplete 即状态机分叉）。
        l.tasks.push_back(std::move(task));
    }
    l.cv.notify_one();
}

void HandlerThreadPool::wait_idle(int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        size_t total = 0;
        for (const auto& lane : lanes_) {
            total += lane->pending.load(std::memory_order_relaxed);
        }
        if (total == 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    size_t total = 0;
    for (const auto& lane : lanes_) {
        total += lane->pending.load(std::memory_order_relaxed);
    }
    if (total > 0) {
        WARN("HandlerThreadPool::wait_idle timeout, {} lane tasks still pending", total);
    }
}

void HandlerThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load()) return;
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& lane : lanes_) {
        std::lock_guard<std::mutex> lock(lane->mutex);
        lane->cv.notify_all();
    }
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    for (auto& lane : lanes_) {
        if (lane->worker.joinable()) lane->worker.join();
    }
}

Reactor::Reactor(CMUniquePtr<ConnectionManager> transport, size_t handler_lanes)
    : transport_(std::move(transport)) {
    if (handler_lanes > 0) {
        // 通用 worker 数量 0：所有并行需求都经 lane 表达（同 conn 串行是硬约束）。
        // +1 是顺序敏感域的保留串行 lane（下标 = handler_lanes）：常规分发
        // 用 conn % handler_lanes，永不落在保留 lane 上。
        handler_lane_count_ = handler_lanes;
        handler_pool_ = CMMakeUnique<HandlerThreadPool>(0, 100, handler_lanes + 1);
    }
}

void Reactor::set_serialized_domain(CMVector<MessageType> types, bool lifecycle_events) {
    serialize_lifecycle_events_ = lifecycle_events;
    for (MessageType t : types) {
        serialized_types_.insert(t);
    }
}

Reactor::~Reactor() {
    stop();
    // 先停 run 循环，再排空 lane（shutdown 语义：队列清空后线程才退出），
    // 保证已提交的 handler（如 stop 屏障等待的 MessageCountReport）执行完毕。
    handler_pool_.reset();
}

void Reactor::on_connect(std::function<void(uint64_t)> handler) {
    connect_handler_ = handler;
}

void Reactor::on_disconnect(std::function<void(uint64_t)> handler) {
    disconnect_handler_ = handler;
}

void Reactor::on_error(std::function<void(uint64_t, int)> handler) {
    error_handler_ = handler;
}

void Reactor::drain_handlers(int timeout_ms) {
    if (handler_pool_) {
        handler_pool_->wait_idle(timeout_ms);
    }
}

void Reactor::run() {
    if (stop_requested_.load()) return;
    running_ = true;
    while (running_) {
        try {
            run_once(10);
        } catch (const std::exception& e) {
            ERR("Reactor::run_once threw exception: {}", e.what());
        } catch (...) {
            ERR("Reactor::run_once threw unknown exception");
        }
    }
}

void Reactor::wait_until_running() const {
    while (!running_.load()) {
        if (stop_requested_.load()) {
            ERR("Reactor::wait_until_running: stop requested before reactor started running");
            assert(false && "Reactor stop requested before running");
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Reactor::run_once(int timeout_ms) {
    auto events = transport_->poll(timeout_ms);
    for (const auto& event : events) {
        handle_event(event);
    }
}

void Reactor::stop() {
    stop_requested_ = true;
    running_ = false;
}

CMSharedPtr<std::mutex> Reactor::acquire_send_mutex(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(conn_send_mutex_map_mutex_);
    auto it = conn_send_mutexes_.find(conn_id);
    if (it != conn_send_mutexes_.end()) {
        return it->second;  // 拷贝 shared_ptr：erase 后 mutex 仍保活到解锁
    }
    auto m = CMMakeShared<std::mutex>();
    conn_send_mutexes_[conn_id] = m;
    return m;
}

void Reactor::remove_send_mutex(uint64_t conn_id) {
    std::lock_guard<std::mutex> lock(conn_send_mutex_map_mutex_);
    conn_send_mutexes_.erase(conn_id);
}

void Reactor::handle_event(const TransportEvent& event) {
    switch (event.type_) {
        case TransportEventType::CONNECT:
            recv_buffers_[event.conn_id_] = "";
            if (connect_handler_) {
                run_event_callback(event.conn_id_,
                    [this, conn = event.conn_id_]() { connect_handler_(conn); });
            }
            break;
            
        case TransportEventType::DATA:
            recv_buffers_[event.conn_id_] += event.data_;
            dispatch_message(event.conn_id_, recv_buffers_[event.conn_id_]);
            break;
            
        case TransportEventType::DISCONNECT:
            if (disconnect_handler_) {
                run_event_callback(event.conn_id_,
                    [this, conn = event.conn_id_]() { disconnect_handler_(conn); });
            }
            recv_buffers_.erase(event.conn_id_);
            remove_send_mutex(event.conn_id_);
            break;
            
        case TransportEventType::ERROR:
            if (error_handler_) {
                run_event_callback(event.conn_id_,
                    [this, conn = event.conn_id_, code = event.error_code_]() {
                        error_handler_(conn, code);
                    });
            }
            recv_buffers_.erase(event.conn_id_);
            remove_send_mutex(event.conn_id_);
            break;
    }
}

void Reactor::run_event_callback(uint64_t conn_id, std::function<void()> cb) {
    if (!handler_pool_) {
        cb();
        return;
    }
    size_t lane = serialize_lifecycle_events_ ? handler_lane_count_
                                              : conn_id % handler_lane_count_;
    handler_pool_->submit_to_lane(lane, std::move(cb));
}

void Reactor::dispatch_message(uint64_t conn_id, CMString& buffer) {
    while (!buffer.empty()) {
        MessageType type = MessageProtocol::get_type(buffer);
        
        auto it = handlers_.find(type);
        if (it == handlers_.end()) {
            uint32_t total_size = MessageProtocol::get_total_size(buffer);
            if (total_size > 0 && buffer.size() >= 4 + total_size) {
                buffer.erase(0, 4 + total_size);
            } else {
                break;
            }
            continue;
        }
        
        uint32_t total_size = MessageProtocol::get_total_size(buffer);
        if (total_size < 1 || buffer.size() < 4 + total_size) {
            // 帧不完整/畸形：与 legacy 行为一致（decode 失败 → handler 未消费
            // → 清空缓冲区），无法解码出有效消息，丢弃残留。
            ERR("dispatch_message: malformed frame type={} len={} buf={}",
                static_cast<int>(type), total_size, buffer.size());
            buffer.clear();
            break;
        }

        if (handler_pool_) {
            // 帧提取（含 recv_buffers_ 推进）留在 reactor 线程；decode + handler
            // 在该 conn 的 lane 上执行，同 conn 严格保序、跨 conn 并行；
            // 顺序敏感域命中类型改投保留串行 lane（跨连接 FIFO）。
            CMString frame = buffer.substr(0, 4 + total_size);
            buffer.erase(0, 4 + total_size);
            auto handler = it->second;
            size_t lane = serialized_types_.count(type) ? handler_lane_count_
                                                        : conn_id % handler_lane_count_;
            handler_pool_->submit_to_lane(
                lane,
                [handler, conn_id, frame = std::move(frame)]() mutable {
                    CMString raw = std::move(frame);
                    CMString before = raw;
                    handler(conn_id, raw);
                    if (raw == before) {
                        ERR("lane handler for type={} did not consume frame ({} bytes)",
                            static_cast<int>(MessageProtocol::get_type(raw)), raw.size());
                    }
                });
        } else {
            CMString temp = buffer;
            auto& handler = it->second;
            handler(conn_id, buffer);
            
            if (buffer == temp) {
                ERR("dispatch_message: handler for type={} did not consume buffer, discarding {} bytes",
                    static_cast<int>(type), buffer.size());
                buffer.clear();
            }
        }
    }
}

}  // namespace fly
