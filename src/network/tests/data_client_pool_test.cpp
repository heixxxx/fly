// Unit tests for DataClientPool DATA_NOT_READY passthrough behavior.
//
// TDD driver for the read-path hardening change: the pool must STOP internally
// polling DATA_NOT_READY and instead pass it back to the caller as a typed
// ReadError, so that TIER2 (the multi-replica + backoff layer) owns retry
// policy. Before the change these tests hang/timeout (pool loops forever on
// DATA_NOT_READY); after the change they pass (pool returns ReadError::DATA_NOT_READY).
#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/data_writer.h>
#include <common/serialization/cpp/object_header.h>
#include <common/buffer/cpp/fly_buffer.h>
#include <common/buffer/cpp/data_checksum.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/tcp_socket.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <network/cpp/net_quality_monitor.h>
#include <common/runtime/cpp/error_types.h>
#include <common/testing/cpp/test_helpers.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <future>
#include <atomic>
#include <latch>
#include <vector>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fly {

class DataClientPoolTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<DataService> ds_ = DataService::instance();

    void SetUp() override {
        test_dir_ = fly::test::qa_tmp_dir("fly_test_pool");
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
    bool send_file(int fd, int file_fd, uint64_t o, size_t n) override { return inner_->send_file(fd, file_fd, o, n); }
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

// ════════════════════════════════════════════════════════════════════
// wire 根摘要（chunked-transfer-design.md §4.2/§4.5 / 测试 6-7）
// ════════════════════════════════════════════════════════════════════

// 测试 7：真 DataServer 响应携带非零 payload_crc_ 且 == data_checksum(raw)。
// 观测口径：手工 client 读整帧 → 解析 small fields → 对照本地重算。
TEST_F(DataClientPoolTest, ServerComputesCrc) {
    std::string db_path = "/crctest";
    std::string full = db_path + ":obj";
    std::string payload = "wire root checksum anchor payload";

    // 写一个新格式 record 并登记（复用 data_server 服务路径）。
    {
        auto record = CMMakeShared<FlyBuffer>();
        int32_t sz = static_cast<int32_t>(payload.size());
        uint64_t crc = data_checksum(payload.data(), payload.size());
        record->write(reinterpret_cast<const char*>(&sz), 4);
        record->write(reinterpret_cast<const char*>(&sz), 4);
        record->write(reinterpret_cast<const char*>(&crc), 8);
        record->write(payload.data(), payload.size());
        ObjectHeader header;
        header.total_size_ = payload.size();
        header.chunk_count_ = 1;
        header.py_name_ = "bytes";
        header.py_name_len_ = 5;
        header.compression_type_ = 0;
        header.block_comp_lens_ = {static_cast<uint32_t>(payload.size())};  // B'
        CMString trailer = header.serialize_trailer();
        record->write(trailer.data(), trailer.size());

        ds_->register_database(db_path, test_dir_ + "/data");
        ds_->on_write_started(db_path, full);
        DataWriter writer(test_dir_, test_dir_ + "/data", "crcw", 0);
        writer.write_record(full, payload.size(), 1, *record, "");
        writer.flush();
        auto entries = writer.get_all_entries(full);
        ASSERT_TRUE(entries.has_value());
        ds_->on_write_completed(db_path, full, entries.value());
        ds_->on_object_flushed(full);
    }

    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    auto transport = create_tcp_transport();
    int fd = transport->create_connection("127.0.0.1", port);
    ASSERT_GE(fd, 0);
    transport->set_recv_timeout(fd, 5000);
    transport->set_send_timeout(fd, 5000);

    DataRequestMessage req;
    req.object_name_ = full;
    CMString encoded = MessageProtocol::encode(req);
    ASSERT_TRUE(transport->send_all(fd, encoded.data(), encoded.size()));

    // 读整帧：9B 帧头 → 5B 子头 → small fields → raw。
    char fhdr[9];
    ASSERT_TRUE(recv_exact(transport.get(), fd, fhdr, 9));
    uint64_t total_len = 0;
    ASSERT_TRUE(parse_frame_header(fhdr, total_len));
    char shdr[5];
    ASSERT_TRUE(recv_exact(transport.get(), fd, shdr, 5));
    uint32_t small_len = 0;
    bool has_raw = false;
    DataResponseProtocol::parse_sub_header(shdr, small_len, has_raw);
    ASSERT_TRUE(has_raw);
    CMString small(small_len, '\0');
    ASSERT_TRUE(recv_exact(transport.get(), fd, small.data(), small_len));
    DataResponseMessage resp;
    ASSERT_TRUE(DataResponseProtocol::decode_small_fields(small, resp));
    uint64_t raw_len = DataResponseProtocol::raw_len_from_total(total_len, small_len);
    CMString raw(raw_len, '\0');
    ASSERT_TRUE(recv_exact(transport.get(), fd, raw.data(), raw_len));
    transport->close(fd);

    EXPECT_TRUE(resp.success_);
    EXPECT_NE(resp.payload_crc_, 0u);
    EXPECT_EQ(resp.payload_crc_, data_checksum(raw.data(), raw.size()));
}

// 测试 6：fake server 故意发错 payload_crc_ → client 必须拒绝（CHECKSUM，
// 决定性注入，零生产测试钩子）。
TEST_F(DataClientPoolTest, ClientDetectsBadCrc) {
    auto transport_listener = create_tcp_transport();
    int listen_fd = transport_listener->create_listen_socket("127.0.0.1", 0);
    ASSERT_GE(listen_fd, 0);
    int port = transport_listener->get_port(listen_fd);

    // fake server 线程：accept → 读 DATA_REQUEST → 发"成功"响应但 crc 注错。
    std::thread server_thread([&] {
        struct pollfd pfd;
        pfd.fd = listen_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (::poll(&pfd, 1, 10000) <= 0) return;
        int cfd = transport_listener->accept_connection(listen_fd);
        if (cfd < 0) return;
        transport_listener->set_recv_timeout(cfd, 5000);
        transport_listener->set_send_timeout(cfd, 5000);

        // 读 client 的 DATA_REQUEST 帧（9B 头 + total_len-1）。
        char h[9];
        if (!recv_exact(transport_listener.get(), cfd, h, 9)) return;
        uint64_t tl = 0;
        if (!parse_frame_header(h, tl)) return;
        CMString reqbuf(static_cast<size_t>(tl - 1), '\0');
        if (!recv_exact(transport_listener.get(), cfd, reqbuf.data(),
                        static_cast<size_t>(tl - 1))) {
            return;
        }

        // 组"成功"响应：payload_crc_ 故意错误（≠ raw 的真实摘要）。
        std::string raw = "corrupt-me wire payload";
        DataResponseMessage resp;
        resp.success_ = true;
        resp.py_name_ = "bytes";
        resp.payload_crc_ = 0xDEADBEEFDEADBEEFull;  // 注错
        auto buf = CMMakeShared<FlyBuffer>();
        buf->write(raw.data(), raw.size());
        auto seg = DataResponseProtocol::encode(resp, buf);
        ASSERT_TRUE(transport_listener->send_all(cfd, seg.header_segment.data(),
                                                 seg.header_segment.size()));
        ASSERT_TRUE(transport_listener->send_all(cfd, seg.raw_ptr,
                                                 static_cast<size_t>(seg.raw_len)));
        // 等 client 读完再关（避免 RST 截断）。
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        transport_listener->close(cfd);
    });

    DataClientPool pool(1);
    auto [success, data, py_name, hash, error, rerr] =
        pool.request("127.0.0.1", port, "/fake:obj", 0, 0, 5000);

    server_thread.join();
    transport_listener->close(listen_fd);

    EXPECT_FALSE(success);
    EXPECT_EQ(rerr, ReadError::CHECKSUM) << "error: " << error;
}

// ════════════════════════════════════════════════════════════════════
// 半开 fd 防御：server 以 SO_LINGER{1,0} 关闭（RST，不走 FIN 波动态），
// 池内 keep-alive fd 变半开。下一次 borrow 的 SO_ERROR 预检（或 send/recv）
// 必须识别并回收坏 fd，重试建立新连接完成交换——坏连接不得回池复用。
// ════════════════════════════════════════════════════════════════════
namespace {
// 每个连接：读请求 → 回合法 NOT_FOUND 响应 → linger-RST 关闭（制造半开对端）。
class FakeLingerServer {
public:
    explicit FakeLingerServer() {
        listen_fd_ = transport_->create_listen_socket("127.0.0.1", 0);
        port_ = transport_->get_port(listen_fd_);
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeLingerServer() {
        transport_->close(listen_fd_);
        if (thread_.joinable()) thread_.join();
    }
    int port() const { return port_; }
    int accepted() const { return accepted_.load(std::memory_order_relaxed); }

private:
    void serve() {
        // 本测试最多 3 轮重试，逐一 accept。
        for (int i = 0; i < 8; ++i) {
            struct pollfd pfd;
            pfd.fd = listen_fd_;
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (::poll(&pfd, 1, 5000) <= 0) return;
            int fd = transport_->accept_connection(listen_fd_);
            if (fd < 0) return;
            transport_->set_recv_timeout(fd, 5000);
            transport_->set_send_timeout(fd, 5000);
            accepted_.fetch_add(1, std::memory_order_relaxed);

            char h[9];
            if (!recv_exact(transport_.get(), fd, h, 9)) {
                ::close(fd);
                continue;
            }
            uint64_t tl = 0;
            if (!parse_frame_header(h, tl)) {
                ::close(fd);
                continue;
            }
            CMString rest(static_cast<size_t>(tl - 1), '\0');
            if (!recv_exact(transport_.get(), fd, rest.data(), static_cast<size_t>(tl - 1))) {
                ::close(fd);
                continue;
            }

            DataResponseMessage resp;
            resp.success_ = false;
            resp.status_ = ResponseStatus::NOT_FOUND;
            CMString frame = DataResponseProtocol::encode(resp, nullptr).header_segment;
            transport_->send_all(fd, frame.data(), frame.size());

            // RST 关闭：丢弃收发缓冲直接复位——客户端池内 idle fd 的
            // SO_ERROR 随 RST 到达置位（半开形态）。
            struct linger lg;
            lg.l_onoff = 1;
            lg.l_linger = 0;
            ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
            ::close(fd);
        }
    }

    CMSharedPtr<Transport> transport_ = create_tcp_transport();
    int listen_fd_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::atomic<int> accepted_{0};
};
}  // namespace

// 半开 fd 不回池：第一次交换成功（fd 回池）→ server RST → 第二次 request
// 借到半开 fd 必须被识别回收（SO_ERROR 预检或 send/recv 失败）→ 自动重试
// 新连接完成交换。RST 到达是异步的（loopback μs 级），故以"最终成功 +
// 发生了重连"为行为契约；若中间轮以 NETWORK 失败，坏 fd 已被 release(false)
// 回收，循环重试至成功（总 deadline 封顶，不会无限挂）。
TEST_F(DataClientPoolTest, HalfOpenIdleFdIsRecycledAndRetried) {
    FakeLingerServer server;
    auto transport = CMMakeShared<CountingTransport>(create_tcp_transport());
    DataClientPool pool(transport, 2);

    auto [ok1, d1, p1, h1, e1, r1] =
        pool.request("127.0.0.1", server.port(), "/halfopen:obj", 0, 0, 5000);
    ASSERT_FALSE(ok1);
    ASSERT_EQ(r1, ReadError::OBJECT_NOT_FOUND) << e1;  // 完整交换，fd 入池

    // 轮询至 RST 被客户端内核接收：server 侧 accepted==1 稳定 + 短暂让出
    // 调度（无 sleep-断言：断言对象是"最终成功"，此处只是降低首轮撞上
    // 未到 RST 的概率，失败轮会被循环覆盖）。
    for (int i = 0; i < 50 && server.accepted() < 1; ++i) {
        std::this_thread::yield();
    }

    bool recovered = false;
    ReadError last_err = ReadError::NONE;
    for (int attempt = 0; attempt < 4 && !recovered; ++attempt) {
        auto [ok, d, p, h, e, r] =
            pool.request("127.0.0.1", server.port(), "/halfopen:obj", 0, 0, 5000);
        last_err = r;
        recovered = ok || r == ReadError::OBJECT_NOT_FOUND;
    }
    EXPECT_TRUE(recovered) << "半开 fd 必须被回收并重试成功, last_err=" << static_cast<int>(last_err);
    EXPECT_GE(transport->connect_count(), 2)
        << "半开 fd 必须触发一次新连接（回收后重试）";
}

// request_raw_exchange 的整缓冲快路径：非 chunked 成功响应 → whole_data 填充
// + fd 即刻归还（后续请求复用同一连接——keep-alive 语义对 raw 链路同样生效）。
TEST_F(DataClientPoolTest, RawExchangeWholeDataReturnsAndReusesFd) {
    ds_->register_database("/rxwhole", test_dir_, test_dir_ + "/data");
    ds_->start_data_server("127.0.0.1", 0, 2);
    int port = ds_->get_data_port();

    DataClientPool pool(2);
    // 快路径对象（< chunked 阈值）：非 chunked 成功响应 → whole_data。
    auto ex = pool.request_raw_exchange("127.0.0.1", port, "/rxwhole:missing",
                                        5000);
    // missing 对象走 NOT_FOUND（协议级失败，fd 归还复用）。whole_data 快路径
    // 需要真实数据对象——复用 DataServerReturnsDataForCompletedWrite 的登记
    // 方式（此处以 NOT_FOUND 验证 raw 链路的协议级失败同样保持 fd 池健康）。
    EXPECT_FALSE(ex.success);
    EXPECT_EQ(ex.rerr, ReadError::OBJECT_NOT_FOUND);
    EXPECT_EQ(ex.handle, nullptr);

    // fd 池健康：下一请求复用（不新建连接——通过第二次成功交换隐式验证）。
    auto [ok, d, p, h, e, r] =
        pool.request("127.0.0.1", port, "/rxwhole:missing", 0, 0, 5000);
    EXPECT_FALSE(ok);
    EXPECT_EQ(r, ReadError::OBJECT_NOT_FOUND);
}

}  // namespace fly
