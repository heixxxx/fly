// Reactor 顺序敏感域（serialized domain）机制单测（P3-26 架构收口）：
//   1. 域内消息跨连接严格 FIFO（前一个未完成时后一个不得执行）；
//   2. 域外消息不受影响——串行 lane 阻塞时其他 conn 的普通消息照常并行执行；
//   3. 生命周期事件（disconnect）入域时与域内消息同序串行。
// 全部用 latch/cv 构造确定性交错，不依赖时序运气。

#include <gtest/gtest.h>
#include <network/cpp/reactor.h>
#include <network/cpp/tcp_connection_manager.h>
#include <common/testing/cpp/test_helpers.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace fly {
namespace {

// 测试编排门闩：一个可等待、可放行的闸门（handler 线程阻塞其上）。
class Gate {
public:
    void enter() {
        {
            std::lock_guard<std::mutex> lk(m_);
            entered_ = true;
        }
        cv_.notify_all();
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [this] { return open_; });
    }
    bool wait_entered(int timeout_ms) {
        std::unique_lock<std::mutex> lk(m_);
        return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                            [this] { return entered_; });
    }
    void open() {
        {
            std::lock_guard<std::mutex> lk(m_);
            open_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    bool entered_ = false;
    bool open_ = false;
};

// 顺序记录器：handler 按完成序 append（mutex 保护），支持有界等待。
class OrderLog {
public:
    void add(uint64_t v) {
        std::lock_guard<std::mutex> lk(m_);
        log_.push_back(v);
        cv_.notify_all();
    }
    CMVector<uint64_t> snapshot() {
        std::lock_guard<std::mutex> lk(m_);
        return log_;
    }
    size_t wait_size_at_least(size_t n, int timeout_ms) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                     [this, n] { return log_.size() >= n; });
        return log_.size();
    }
    // 有界确认 size 停留在 n（串行 lane 阻塞时后继不得执行）。
    bool wait_stays_at(size_t n, int timeout_ms) {
        std::unique_lock<std::mutex> lk(m_);
        bool grew = cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                                 [this, n] { return log_.size() > n; });
        return !grew && log_.size() == n;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    CMVector<uint64_t> log_;
};

HeartbeatMessage make_heartbeat(uint64_t worker_id) {
    HeartbeatMessage msg;
    msg.header_.type_ = MessageType::HEARTBEAT;
    msg.header_.message_id_ = worker_id;
    msg.header_.timestamp_ = 0;
    msg.worker_id_ = worker_id;
    return msg;
}

VarSetMessage make_var_set(const CMString& name) {
    VarSetMessage msg;
    msg.header_.type_ = MessageType::VAR_SET;
    msg.header_.message_id_ = 1;
    msg.header_.timestamp_ = 0;
    msg.var_name_ = name;
    msg.value_ = "v";
    msg.type_name_ = "str";
    return msg;
}

class ReactorSerializedDomainTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_transport_ = CMMakeUnique<TcpConnectionManager>();
        server_transport_->listen("127.0.0.1", 0);
        port_ = server_transport_->get_bound_port();
    }
    void TearDown() override {
        if (reactor_) {
            reactor_->stop();
            reactor_->drain_handlers();
        }
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
        for (auto* t : clients_) t->close_all();
    }

    // 顺序建 N 个客户端连接（server 侧 conn_id 依次 1..N），并等 CONNECT 分发。
    // 注意：conn_id 是 transport 实例内计数（每个 client 自己的 id 都是 1），
    // send 用各 client 自己 connect 返回的 id。
    void connect_clients_and_run(size_t n, bool lifecycle_events) {
        for (size_t i = 0; i < n; ++i) {
            auto* t = new TcpConnectionManager();
            uint64_t cid = t->connect("127.0.0.1", port_);
            ASSERT_NE(cid, 0u);
            client_conn_ids_.push_back(cid);
            clients_.push_back(t);
        }
        reactor_ = CMMakeUnique<Reactor>(std::move(server_transport_), /*lanes=*/2);
        reactor_->set_serialized_domain({MessageType::HEARTBEAT}, lifecycle_events);
        reactor_->on_connect([this](uint64_t) { connected_++; });
    }
    void run_reactor() {
        reactor_thread_ = std::thread([this] { reactor_->run(); });
        reactor_->wait_until_running();
        test::wait_for([&] { return connected_.load() >= clients_.size(); }, 100, 10);
        ASSERT_EQ(connected_.load(), clients_.size());
    }

    TcpConnectionManager* client(size_t i) { return clients_[i]; }
    uint64_t client_conn(size_t i) { return client_conn_ids_[i]; }

    CMUniquePtr<TcpConnectionManager> server_transport_;
    CMUniquePtr<Reactor> reactor_;
    std::thread reactor_thread_;
    CMVector<TcpConnectionManager*> clients_;
    CMVector<uint64_t> client_conn_ids_;
    int port_ = 0;
    std::atomic<size_t> connected_{0};
};

// 域内消息跨连接 FIFO：conn A 的域内 handler 阻塞时，conn B 的域内消息
// 不得执行；conn C 的域外消息照常并行执行；放行后按到达序完成。
TEST_F(ReactorSerializedDomainTest, DomainMessagesFifoAcrossConnections) {
    Gate first_blocks;
    OrderLog beats;  // HEARTBEAT（域内）完成记录
    OrderLog vars;   // VAR_SET（域外）完成记录

    connect_clients_and_run(3, /*lifecycle_events=*/false);
    reactor_->register_handler<HeartbeatMessage>(
        [&](uint64_t conn_id, const HeartbeatMessage&) {
            if (conn_id == 1) first_blocks.enter();  // 首个域内 handler 阻塞
            beats.add(conn_id);
        });
    reactor_->register_handler<VarSetMessage>(
        [&](uint64_t conn_id, const VarSetMessage&) { vars.add(conn_id); });
    run_reactor();

    // conn1 域内消息：handler 阻塞在 Gate 上。
    client(0)->send(client_conn(0), MessageProtocol::encode(make_heartbeat(1)));
    ASSERT_TRUE(first_blocks.wait_entered(5000)) << "first domain handler must start";
    EXPECT_EQ(beats.snapshot().size(), 0u) << "blocked handler must not complete";

    // conn2 域内消息：必须排队（串行 lane 被 conn1 的 handler 占住）。
    client(1)->send(client_conn(1), MessageProtocol::encode(make_heartbeat(2)));
    EXPECT_TRUE(beats.wait_stays_at(0, 300))
        << "second domain message must NOT run while first is blocked";

    // conn3 域外消息：并行执行，不受串行 lane 阻塞影响。
    client(2)->send(client_conn(2), MessageProtocol::encode(make_var_set("x")));
    EXPECT_EQ(vars.wait_size_at_least(1, 5000), 1u)
        << "non-domain message must run in parallel with blocked serialized lane";

    // 放行：conn1 handler 完成 → 排队的 conn2 消息按序执行。
    first_blocks.open();
    EXPECT_EQ(beats.wait_size_at_least(2, 5000), 2u);
    auto order = beats.snapshot();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1u);
    EXPECT_EQ(order[1], 2u) << "domain messages must complete in FIFO order";
}

// 生命周期事件入域：disconnect 回调阻塞时，另一连接的域内消息排队等待，
// 域外消息照常并行——P3-26 场景（REGISTER vs DISCONNECT 交错）的机制级钉子。
TEST_F(ReactorSerializedDomainTest, LifecycleEventsSerializedWithDomain) {
    Gate disconnect_blocks;
    OrderLog beats;
    OrderLog vars;

    connect_clients_and_run(3, /*lifecycle_events=*/true);
    reactor_->on_disconnect([&](uint64_t conn_id) {
        if (conn_id == 1) disconnect_blocks.enter();
        beats.add(1000 + conn_id);  // 断连完成记录（1000+conn 与消息区分）
    });
    reactor_->register_handler<HeartbeatMessage>(
        [&](uint64_t conn_id, const HeartbeatMessage&) { beats.add(conn_id); });
    reactor_->register_handler<VarSetMessage>(
        [&](uint64_t conn_id, const VarSetMessage&) { vars.add(conn_id); });
    run_reactor();

    // conn1 断连：disconnect 回调（域内）阻塞。
    client(0)->close_all();
    ASSERT_TRUE(disconnect_blocks.wait_entered(5000))
        << "disconnect callback must start and block";

    // conn2 域内消息：排队等待断连处理完成，不得越过。
    client(1)->send(client_conn(1), MessageProtocol::encode(make_heartbeat(2)));
    EXPECT_TRUE(beats.wait_stays_at(0, 300))
        << "domain message must NOT overtake blocked disconnect callback";

    // conn3 域外消息：并行不受影响。
    client(2)->send(client_conn(2), MessageProtocol::encode(make_var_set("y")));
    EXPECT_EQ(vars.wait_size_at_least(1, 5000), 1u)
        << "non-domain message must run while serialized lane blocked";

    // 放行：断连完成 → 排队的域内消息执行。
    disconnect_blocks.open();
    EXPECT_EQ(beats.wait_size_at_least(2, 5000), 2u);
    auto order = beats.snapshot();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1001u) << "disconnect (conn1) must complete first";
    EXPECT_EQ(order[1], 2u) << "queued heartbeat (conn2) must follow in FIFO";
}

}  // namespace
}  // namespace fly
