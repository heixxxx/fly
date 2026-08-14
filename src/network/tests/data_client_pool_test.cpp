// Unit tests for DataClientPool DATA_NOT_READY passthrough behavior.
//
// TDD driver for the read-path hardening change: the pool must STOP internally
// polling DATA_NOT_READY and instead pass it back to the caller as a typed
// ReadError, so that TIER2 (the multi-replica + backoff layer) owns retry
// policy. Before the change these tests hang/timeout (pool loops forever on
// DATA_NOT_READY); after the change they pass (pool returns ReadError::DATA_NOT_READY).
#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/net_quality_monitor.h>
#include <common/cpp/error_types.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>
#include <latch>
#include <vector>

namespace fly {

class DataClientPoolTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<DataService> ds_ = DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_pool_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        ds_->reset();
    }

    void TearDown() override {
        ds_->stop_data_server();
        std::filesystem::remove_all(test_dir_);
    }

    // Bring up a DataServer whose state for `full` is "write in progress"
    // (on_write_started without on_write_completed), so reads return DATA_NOT_READY.
    int start_server_with_in_progress_write(const CMString& db_path, const CMString& full) {
        ds_->register_database(db_path, test_dir_, test_dir_ + "/data");
        ds_->on_write_started(db_path, full);
        ds_->start_data_server("127.0.0.1", 0, 2);
        return ds_->get_data_port();
    }
};

// After the change: pool returns ReadError::DATA_NOT_READY instead of looping.
TEST_F(DataClientPoolTest, DataNotReadyIsPassthroughNotPolled) {
    std::string db_path = "/testd";
    std::string full = db_path + ":notready";
    int port = start_server_with_in_progress_write(db_path, full);

    DataClientPool pool(2);
    uint64_t rid = 0;

    // Run in a future with a hard wall-clock cap: before the change the pool
    // loops forever on DATA_NOT_READY, so the future would time out. After the
    // change it resolves immediately with ReadError::DATA_NOT_READY.
    auto fut = std::async(std::launch::async, [&] {
        auto [success, data, py_name, hash, error, rerr] =
            pool.request("127.0.0.1", port, full, 0, rid, 5000);
        return std::make_tuple(success, rerr);
    });

    ASSERT_EQ(fut.wait_for(std::chrono::seconds(5)), std::future_status::ready)
        << "pool.request hung on DATA_NOT_READY (still internally polling)";
    auto [success, rerr] = fut.get();

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::DATA_NOT_READY);
}

// Non-protocol errors map to ReadError::NETWORK.
TEST_F(DataClientPoolTest, ConnectionFailureMapsToNetworkError) {
    DataClientPool pool(2);
    // Port 1 is reserved/closed on typical systems → connect failure.
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", 1, "dead:beef", 0, 0, 1000);

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::NETWORK);
}

// OBJECT_NOT_FOUND is still passed through, typed.
TEST_F(DataClientPoolTest, ObjectNotFoundIsTyped) {
    std::string db_path = "/teste";
    ds_->register_database(db_path, test_dir_, test_dir_ + "/data");
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", port, db_path + ":missing", 0, 0, 5000);

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::OBJECT_NOT_FOUND);
}

// A completed exchange (even a protocol-level failure like OBJECT_NOT_FOUND)
// feeds a passive RTT sample into NetQualityMonitor, so the host becomes ranked.
TEST_F(DataClientPoolTest, CompletedExchangeFeedsPassiveRtt) {
    NetQualityMonitor::instance().clear();
    std::string db_path = "/testf";
    ds_->register_database(db_path, test_dir_, test_dir_ + "/data");
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", port, db_path + ":missing", 0, 0, 5000);

    ASSERT_EQ(rerr, ReadError::OBJECT_NOT_FOUND);
    // A full round-trip completed → the loopback host now has a positive score.
    EXPECT_GT(NetQualityMonitor::instance().score("127.0.0.1"), 0.0);

    NetQualityMonitor::instance().clear();
}

// A connection that never completes (no server) must NOT record a sample.
TEST_F(DataClientPoolTest, FailedConnectionFeedsNoSample) {
    NetQualityMonitor::instance().clear();
    DataClientPool pool(2);
    pool.request("127.0.0.1", 1, "dead:beef", 0, 0, 1000);  // connect fails
    EXPECT_DOUBLE_EQ(NetQualityMonitor::instance().score("127.0.0.1"), 0.0);
}

// ════════════════════════════════════════════════════════════════════
// keep-alive 连接池改造测试
// 用 CountingTransport 装饰真实 TCP transport，计数 create_connection/close
// 调用以观测 fd 复用、并发多 fd、反倾斜淘汰行为。
// ════════════════════════════════════════════════════════════════════
class CountingTransport : public Transport {
    CMSharedPtr<Transport> inner_;
    std::atomic<int> connect_count_{0};
    std::atomic<int> close_count_{0};
    // 并发用例的确定性 gate：create_connection 先报到并等放行，保证 N 个并发
    // 请求都已进入 connect 阶段（各自已预留 fd 配额）才真正建连。latch 屏障
    // 不够——barrier 释放后线程仍可能被 OS 抢占到首个请求完整归还 fd 之后，
    // 高负载下 connect_count 误报为 1（pre-push hook 实测两次）。
    std::atomic<int> gate_expected_{0};
    std::atomic<int> gate_arrived_{0};
    std::atomic<bool> gate_open_{true};
public:
    explicit CountingTransport(CMSharedPtr<Transport> inner) : inner_(std::move(inner)) {}
    int connect_count() const { return connect_count_.load(std::memory_order_relaxed); }
    int close_count() const { return close_count_.load(std::memory_order_relaxed); }

    void arm_connect_gate(int expected) {
        gate_expected_.store(expected, std::memory_order_release);
        gate_arrived_.store(0, std::memory_order_relaxed);
        gate_open_.store(false, std::memory_order_release);
    }
    int gate_arrived_count() const { return gate_arrived_.load(std::memory_order_acquire); }
    void release_connect_gate() { gate_open_.store(true, std::memory_order_release); }

    int create_listen_socket(const CMString& h, int p) override { return inner_->create_listen_socket(h, p); }
    int accept_connection(int lfd) override { return inner_->accept_connection(lfd); }
    int create_connection(const CMString& h, int p) override {
        connect_count_.fetch_add(1, std::memory_order_relaxed);
        if (gate_expected_.load(std::memory_order_acquire) > 0) {
            gate_arrived_.fetch_add(1, std::memory_order_acq_rel);
            // 自旋等放行（5s 兜底防意外死锁；正常路径主线程见全员到达即放行）
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!gate_open_.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        }
        return inner_->create_connection(h, p);
    }
    void set_nodelay(int fd) override { inner_->set_nodelay(fd); }
    void set_nonblocking(int fd) override { inner_->set_nonblocking(fd); }
    void set_recv_timeout(int fd, int t) override { inner_->set_recv_timeout(fd, t); }
    void set_send_timeout(int fd, int t) override { inner_->set_send_timeout(fd, t); }
    ssize_t send(int fd, const char* d, size_t n) override { return inner_->send(fd, d, n); }
    ssize_t recv(int fd, char* b, size_t n) override { return inner_->recv(fd, b, n); }
    bool send_all(int fd, const char* d, size_t n) override { return inner_->send_all(fd, d, n); }
    bool sendv(int fd, const struct iovec* iov, int c) override { return inner_->sendv(fd, iov, c); }
    int get_port(int fd) override { return inner_->get_port(fd); }
    void close(int fd) override {
        close_count_.fetch_add(1, std::memory_order_relaxed);
        inner_->close(fd);
    }
};

// 连续两次 request 同一 peer：第二次应复用第一次归还的 idle fd（connect 仅 1 次）。
TEST_F(DataClientPoolTest, ReusesFdAcrossRequestsToSamePeer) {
    ds_->register_database("/reuse", test_dir_, test_dir_ + "/data");
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    auto transport = CMMakeShared<CountingTransport>(create_tcp_transport());
    DataClientPool pool(transport, 2);

    auto r1 = pool.request("127.0.0.1", port, "/reuse:missing", 0, 0, 5000);
    auto r2 = pool.request("127.0.0.1", port, "/reuse:missing", 0, 0, 5000);

    EXPECT_EQ(std::get<5>(r1), ReadError::OBJECT_NOT_FOUND);
    EXPECT_EQ(std::get<5>(r2), ReadError::OBJECT_NOT_FOUND);
    // 两次完整交换，第二次复用 idle fd → 仅 connect 一次
    EXPECT_EQ(transport->connect_count(), 1);
}

// 并发 request 同一 peer：单 fd 同步收发无法并行，必然创建多个 fd 并行传输。
// 确定性方案：transport connect gate——等到 4 个请求都到达 create_connection
// （各自已预留 fd 配额，互不可能复用彼此的 fd）才放行建连，connect_count
// 与线程调度顺序完全无关。
TEST_F(DataClientPoolTest, ConcurrentRequestsToSamePeerUseMultipleFds) {
    ds_->register_database("/conc", test_dir_, test_dir_ + "/data");
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    auto transport = CMMakeShared<CountingTransport>(create_tcp_transport());
    transport->arm_connect_gate(4);
    DataClientPool pool(transport, 4);  // 并发上限 4

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&]() {
            pool.request("127.0.0.1", port, "/conc:missing", 0, 0, 5000);
        });
    }
    // 等 4 个请求都到达 connect 点（5s 兜底：若产品 bug 导致少于 4 个到达，
    // 放行后 connect_count 断言会失败暴露，而非死等）。
    auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (transport->gate_arrived_count() < 4 &&
           std::chrono::steady_clock::now() < wait_deadline) {
        std::this_thread::yield();
    }
    transport->release_connect_gate();
    for (auto& t : threads) t.join();

    // gate 保证 4 次调用 create_connection（计数在 gate 前递增，含失败路径）。
    EXPECT_EQ(transport->connect_count(), 4);
}

// 达 fd 上限(2×pool_size)后为新 peer 新建连接，触发反倾斜淘汰：
// 用 0.0.0.0 server + 127.0.0.x loopback 别名制造多个不同 peer key。
TEST_F(DataClientPoolTest, EvictsByAntiSkewWhenFdLimitReached) {
    ds_->register_database("/evict", test_dir_, test_dir_ + "/data");
    ds_->start_data_server("0.0.0.0", 0, 2);
    int port = ds_->get_data_port();

    auto transport = CMMakeShared<CountingTransport>(create_tcp_transport());
    DataClientPool pool(transport, 1);  // pool_size=1 → max_fd=2

    pool.request("127.0.0.1", port, "/evict:m", 0, 0, 5000);
    pool.request("127.0.0.2", port, "/evict:m", 0, 0, 5000);
    pool.request("127.0.0.3", port, "/evict:m", 0, 0, 5000);

    // 3 个不同 peer 各 connect 一次；max_fd=2 → 第三次必然淘汰过 ≥1 个 idle
    EXPECT_EQ(transport->connect_count(), 3);
    EXPECT_GE(transport->close_count(), 1);
}

}  // namespace fly
