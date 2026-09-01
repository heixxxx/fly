// PeerRpcServer 单元测试（2026-08-16 补覆盖：v2 daemon 通信底座此前零单测，
// 仅 qa/solver 的 v2 case 兜底）。真实 localhost TCP 验证：
//   listen 端口分配 / 端到端 RPC 往返 / 异步响应 / 失败通知 / BYE 优雅关闭 /
//   connect 重试 / stop 清理。

#include <gtest/gtest.h>
#include <agent/cpp/peer_rpc_server.h>
#include <storage/cpp/pipeline.h>
#include <common/cpp/data_checksum.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace fly {
namespace {

// 回调同步等待辅助：测试线程阻塞到异步回调到达（带超时防挂死）。
struct CallbackLatch {
    void notify(uint64_t rpc_id, uint8_t status, const CMString& payload) {
        std::lock_guard<std::mutex> lk(m_);
        got = true;
        rpc_id_ = rpc_id;
        status_ = status;
        payload_ = payload;
        cv_.notify_all();
    }
    bool wait(int timeout_s = 5) {
        std::unique_lock<std::mutex> lk(m_);
        return cv_.wait_for(lk, std::chrono::seconds(timeout_s), [this] { return got; });
    }
    bool fired() const { std::lock_guard<std::mutex> lk(m_); return got; }
    uint64_t rpc_id_ = 0;
    uint8_t status_ = 0;
    CMString payload_;
private:
    bool got = false;
    mutable std::mutex m_;
    std::condition_variable cv_;
};

struct DisconnectLatch {
    void notify(uint64_t conn_id) {
        std::lock_guard<std::mutex> lk(m_);
        got = true;
        conn_id_ = conn_id;
        cv_.notify_all();
    }
    bool wait(int timeout_s = 5) {
        std::unique_lock<std::mutex> lk(m_);
        return cv_.wait_for(lk, std::chrono::seconds(timeout_s), [this] { return got; });
    }
    bool fired() const { std::lock_guard<std::mutex> lk(m_); return got; }
    uint64_t conn_id_ = 0;
private:
    bool got = false;
    mutable std::mutex m_;
    std::condition_variable cv_;
};

// PeerStreamBuffer 测试数据（普通字符串，长度 11）。
inline CMString MakePeerTestData() { return CMString("payload-abc"); }

class PeerRpcServerTest : public ::testing::Test {
protected:
    void TearDown() override {
        if (server) server->stop();
        if (client) client->stop();
    }

    // 确定性伪随机字节（LFSR，高熵可复现）。
    static std::string MakePseudoRandomBytes(size_t n, uint32_t seed = 0x12345678) {
        std::string out(n, '\0');
        uint32_t x = seed;
        for (size_t i = 0; i < n; i++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            out[i] = static_cast<char>(x & 0xFF);
        }
        return out;
    }

    // 起 server + client 并建连。返回连接 id。
    uint64_t setup_connected_pair(PeerRpcServer::RequestHandler handler) {
        int port = server->listen("127.0.0.1", 0, handler);
        EXPECT_GT(port, 0);
        if (port <= 0) return 0;
        uint64_t conn = client->connect_peer("127.0.0.1", port);
        EXPECT_NE(conn, 0u);
        return conn;
    }

    std::unique_ptr<PeerRpcServer> server = std::make_unique<PeerRpcServer>();
    std::unique_ptr<PeerRpcServer> client = std::make_unique<PeerRpcServer>();
};

TEST(PeerStreamBufferTest, FileProtocolReadsAcrossCursor) {
    // read/readline/readinto/seek 全协议：读端游标推进、readinto 直填外部
    // 缓冲（pickle.load 路径）、readline 按行截断、seek 重定位。
    // 长度构造：嵌 NUL 的二进制数据不能走 strlen ctor（会在 \x00 截断）。
    const char* kRaw = "hello world\nsecond line\n\xff\x00" "binary";
    PeerStreamBuffer b(CMString(kRaw, 32));
    EXPECT_EQ(b.size(), 32u);

    CMString r = b.read(5);
    EXPECT_EQ(r, "hello");
    EXPECT_EQ(b.pos(), 5u);

    CMString line = b.readline();   // 从 pos=5 读到行尾
    EXPECT_EQ(line, " world\n");
    EXPECT_EQ(b.pos(), 12u);

    char dst[8] = {};
    EXPECT_EQ(b.readinto(dst, 8), 8u);   // "second l"
    EXPECT_EQ(CMString(dst, 8), "second l");

    b.seek(0);
    EXPECT_EQ(b.read(100), CMString(kRaw, 32));
    // 末尾读穿：take 收敛到剩余量。
    EXPECT_EQ(b.read(10), CMString());
}

TEST(PeerStreamBufferTest, MovePreservesData) {
    // move 构造：交付链 move 语义（零拷贝包装）下数据完整。
    CMString src = MakePeerTestData();
    PeerStreamBuffer b(std::move(src));
    EXPECT_EQ(b.size(), 11u);
    EXPECT_EQ(b.read(11), CMString("payload-abc"));
}

TEST_F(PeerRpcServerTest, ListenAllocatesPortAndStopCleansUp) {
    EXPECT_FALSE(server->is_running());
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&) {
                                  return std::optional<CMString>{};
                              });
    EXPECT_GT(port, 0);
    EXPECT_TRUE(server->is_running());
    server->stop();
    EXPECT_FALSE(server->is_running());
}

TEST_F(PeerRpcServerTest, StreamRequestLargePayloadDeliveredIntact) {
    // 流式请求（10MB 伪随机，> 4MB 帧 → 多帧）：块级 CRC + END 对账后
    // handler 收到完整明文。
    const std::string payload = MakePseudoRandomBytes(10 * 1024 * 1024);
    const uint64_t expect_crc = data_checksum(payload.data(), payload.size());
    std::atomic<bool> got{false};
    std::atomic<uint64_t> got_crc{0};
    std::atomic<size_t> got_size{0};
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString& p) {
            got_size = p.size();
            got_crc = data_checksum(p.data(), p.size());
            got = true;
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    PeerStreamWriter w(client.get(), conn, /*rpc_id=*/9, /*direction=*/0,
                       CompressionType::LZ4, -1);
    ASSERT_TRUE(w.ok());
    w.write(payload.data(), payload.size());
    ASSERT_TRUE(w.finish());
    EXPECT_EQ(w.total_uncompressed(), payload.size());

    for (int i = 0; i < 100 && !got; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(got.load());
    EXPECT_EQ(got_size.load(), payload.size());
    EXPECT_EQ(got_crc.load(), expect_crc);
}

TEST_F(PeerRpcServerTest, StreamMultiBlockMixedCompressibilityIntact) {
    // 多块流三路径覆盖：可压缩段（压缩路径）+ 高熵段（85% 规则 raw 直通）
    // 交替，且按 7KB 小增量写入（块边界跨 write 调用累积）——逐块直发帧
    // 下两种块形态的记录序列化与完整性都不破。位置标记（每 MB 首部写入
    // 段序号）使错位可定位。
    const size_t kSeg = 1024 * 1024;
    const int kSegs = 12;
    std::string payload(static_cast<size_t>(kSegs) * kSeg, '\0');
    for (int s = 0; s < kSegs; s++) {
        char* seg = &payload[static_cast<size_t>(s) * kSeg];
        std::memcpy(seg, &s, sizeof(s));   // 位置标记
        if (s % 2 == 0) {
            const std::string rand_mb = MakePseudoRandomBytes(kSeg, 0xABCD0000 + s);
            std::memcpy(seg, rand_mb.data(), kSeg);
        } else {
            // 可压缩段：1KB 模式重复（位置标记在首 4B，模式内恒定）。
            const std::string pat = MakePseudoRandomBytes(1024, 0x11110000 + s);
            for (size_t off = 0; off < kSeg; off += 1024) {
                std::memcpy(seg + off, pat.data(), 1024);
            }
        }
    }
    const uint64_t expect_crc = data_checksum(payload.data(), payload.size());
    std::atomic<bool> got{false};
    std::atomic<uint64_t> got_crc{0};
    std::atomic<size_t> got_size{0};
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString& p) {
            got_size = p.size();
            got_crc = data_checksum(p.data(), p.size());
            got = true;
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    PeerStreamWriter w(client.get(), conn, /*rpc_id=*/11, /*direction=*/0,
                       CompressionType::LZ4, -1);
    ASSERT_TRUE(w.ok());
    for (size_t off = 0; off < payload.size(); off += 7 * 1024) {
        const size_t n = std::min<size_t>(7 * 1024, payload.size() - off);
        w.write(payload.data() + off, n);
    }
    ASSERT_TRUE(w.finish());
    EXPECT_EQ(w.total_uncompressed(), payload.size());

    for (int i = 0; i < 100 && !got; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_TRUE(got.load());
    EXPECT_EQ(got_size.load(), payload.size());
    EXPECT_EQ(got_crc.load(), expect_crc);
}

TEST_F(PeerRpcServerTest, StreamResponseRoundtripSmallPayload) {
    // 统一流式协议：小 payload（16B，单块流）请求 → 流式响应 echo。
    // START/DATA/END 三帧合并语义下小消息同样走管线（无阈值裁定）。
    const std::string payload = MakePseudoRandomBytes(16);
    CallbackLatch latch;
    client->set_response_handler([&latch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload_in) {
        latch.notify(rpc_id, status, payload_in);
    });

    uint64_t conn = setup_connected_pair(
        [&](uint64_t c, uint64_t rid, uint64_t, const CMString& p) {
            EXPECT_EQ(p, payload);
            uint64_t total = 0;
            uint32_t chunks = 0;
            EXPECT_TRUE(server->send_stream_payload(c, rid, /*direction=*/1, p,
                                                    CompressionType::LZ4, -1,
                                                    total, chunks));
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    PeerStreamWriter w(client.get(), conn, /*rpc_id=*/77, /*direction=*/0,
                       CompressionType::LZ4, -1);
    ASSERT_TRUE(w.ok());
    w.write(payload.data(), payload.size());
    ASSERT_TRUE(w.finish());

    ASSERT_TRUE(latch.wait()) << "streamed response should arrive";
    EXPECT_EQ(latch.rpc_id_, 77u);
    EXPECT_EQ(latch.status_, static_cast<uint8_t>(PeerRpcWireStatus::OK));
    EXPECT_EQ(latch.payload_, payload);
}

TEST_F(PeerRpcServerTest, StreamTruncatedVerifyClosesConnection) {
    // END 对账失配：零容忍——坏流不交付（handler 不触发），server 主动
    // close 使 client 侧收到 DISCONNECT（调用方等待语义由 DISCONNECT 兜底）。
    // shared 持有：server close 引发的 client DISCONNECT 可能在测试函数
    // 返回（TearDown stop）之后到达，handler 不得引用栈上对象。
    auto dlatch = std::make_shared<DisconnectLatch>();
    client->set_disconnect_handler([dlatch](uint64_t c) { dlatch->notify(c); });
    std::atomic<bool> handler_fired{false};
    uint64_t conn = setup_connected_pair([&](uint64_t, uint64_t, uint64_t,
                                             const CMString&) {
        handler_fired = true;
        return std::nullopt;
    });
    ASSERT_NE(conn, 0u);

    // 发 START + 合法块流（真实管线产出）+ END 谎报 total —— 对账失配路径
    // （块流必须合法：块头自描述，非法头会让解析器等待而非失配）。
    EXPECT_TRUE(client->send_stream_start(conn, 5, 0,
                                          static_cast<uint8_t>(CompressionType::NONE)));
    std::string block_stream;
    {
        fly::EmitFn emit = [&](const char* d, size_t n) { block_stream.append(d, n); };
        // NONE 管线：无压缩 Stage，但末端 BlockHeaderStage 必须在——
        // 块记录（[unc][comp][crc][payload]）由它产出（空 Stage 序列的管线
        // 不 emit 任何字节——曾致 DATA 帧只有 29B 头）。
        fly::WritePipeline wp(
            fly::make_file_write_pipeline(nullptr, 32 * 1024, emit, 85, -1));
        const std::string data = MakePseudoRandomBytes(4096);
        wp.write(data.data(), data.size());
        wp.finish();
    }
    EXPECT_TRUE(client->send_stream_data(conn, block_stream.data(),
                                         block_stream.size()));
    EXPECT_TRUE(client->send_stream_end(conn, 5, /*total_uncompressed=*/999999,
                                        /*chunk_count=*/1,
                                        /*consumed=*/block_stream.size()));
    for (int i = 0; i < 100 && !handler_fired; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_FALSE(handler_fired.load()) << "对账失配不得把坏流当 payload 交付";
    for (int i = 0; i < 100 && !dlatch->fired(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(dlatch->fired()) << "零容忍：对账失配必须断连暴露";
}

TEST_F(PeerRpcServerTest, EndToEndRoundTrip) {
    CallbackLatch latch;
    client->set_response_handler([&latch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload) {
        latch.notify(rpc_id, status, payload);
    });

    uint64_t conn = setup_connected_pair(
        [](uint64_t, uint64_t, uint64_t src, const CMString& payload) {
            EXPECT_EQ(src, 7u) << "src_worker_id must round-trip";
            return std::optional<CMString>{"echo:" + payload};
        });
    ASSERT_NE(conn, 0u);

    EXPECT_TRUE(client->send_request(conn, /*rpc_id=*/42, /*src_worker_id=*/7, "ping"));
    ASSERT_TRUE(latch.wait()) << "response should arrive";
    EXPECT_EQ(latch.rpc_id_, 42u);
    EXPECT_EQ(latch.status_, static_cast<uint8_t>(PeerRpcWireStatus::OK));
    EXPECT_EQ(latch.payload_, "echo:ping");
    EXPECT_TRUE(client->is_connected(conn));
}

TEST_F(PeerRpcServerTest, DeferredResponseViaSendResponse) {
    // handler 返回 nullopt（不立即回）→ 测试侧稍后 send_response → 客户端仍收到。
    CallbackLatch latch;
    client->set_response_handler([&latch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload) {
        latch.notify(rpc_id, status, payload);
    });

    uint64_t server_conn = 0;
    uint64_t conn = setup_connected_pair(
        [&server_conn](uint64_t conn_id, uint64_t rpc_id, uint64_t, const CMString&) {
            server_conn = conn_id;   // 服务端视角的连接 id
            return std::optional<CMString>{};   // 不立即响应
        });
    ASSERT_NE(conn, 0u);

    EXPECT_TRUE(client->send_request(conn, /*rpc_id=*/99, 1, "defer"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_GT(server_conn, 0u) << "server should have received the request";

    EXPECT_TRUE(server->send_response(server_conn, /*rpc_id=*/99,
                                      static_cast<uint8_t>(PeerRpcWireStatus::OK), "late"));
    ASSERT_TRUE(latch.wait());
    EXPECT_EQ(latch.rpc_id_, 99u);
    EXPECT_EQ(latch.payload_, "late");
}

TEST_F(PeerRpcServerTest, NotifyFailurePropagatesStatus) {
    CallbackLatch latch;
    client->set_response_handler([&latch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload) {
        latch.notify(rpc_id, status, payload);
    });

    // 服务端 handler 直接 notify 失败。
    uint64_t server_conn = 0;
    uint64_t conn = setup_connected_pair(
        [&server_conn, this](uint64_t conn_id, uint64_t, uint64_t, const CMString&) {
            server_conn = conn_id;
            server->notify_failure(conn_id, "check failed: solver diverged");
            return std::optional<CMString>{};
        });
    ASSERT_NE(conn, 0u);

    EXPECT_TRUE(client->send_request(conn, /*rpc_id=*/5, 1, "req"));
    ASSERT_TRUE(latch.wait());
    EXPECT_EQ(latch.status_, static_cast<uint8_t>(PeerRpcWireStatus::NOTIFY_FAILURE));
    EXPECT_NE(latch.payload_.find("diverged"), CMString::npos);
}

TEST_F(PeerRpcServerTest, SendByeGracefulCloseWithoutDisconnectCallback) {
    // BYE 是正常关闭路径：不触发 disconnect_handler（那是异常断连专用——
    // 用于 fail pending RPC；正常握手关闭不应误伤）。
    DisconnectLatch latch;
    client->set_disconnect_handler([&latch](uint64_t conn_id) { latch.notify(conn_id); });

    uint64_t conn = setup_connected_pair(
        [](uint64_t, uint64_t, uint64_t, const CMString&) {
            return std::optional<CMString>{"ack"};
        });
    ASSERT_NE(conn, 0u);
    // 先做一次 RPC 往返（连接进入活跃状态，排除 recv_bufs_ 未建条目的干扰）。
    CallbackLatch rl;
    client->set_response_handler([&rl](uint64_t, uint64_t rpc_id, uint8_t st,
                                       const CMString& pl) { rl.notify(rpc_id, st, pl); });
    ASSERT_TRUE(client->send_request(conn, 1, 1, "warm"));
    ASSERT_TRUE(rl.wait());

    EXPECT_TRUE(client->send_bye(conn));
    EXPECT_FALSE(client->is_connected(conn));
    // 正常关闭：回调不应触发。等 300ms 确认静默。
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(latch.fired()) << "BYE graceful close must NOT fire disconnect handler";
}

TEST_F(PeerRpcServerTest, ByeAckDisconnectRaceDoesNotFireDisconnectHandler) {
    // P3-25 回归：服务端回 BYE_ACK 后立即 close，客户端的 DISCONNECT 事件与
    // BYE_ACK 几乎同时到达。bye_closed_conns_ 若由 send_bye 调用方线程在
    // cv 唤醒后标记，server_loop 处理 DISCONNECT 时标记可能尚未落位 →
    // 优雅关闭误触发 disconnect_handler（50 轮稳定性第 9 轮实测复现）。
    // 确定性构造：bye_wake_hook 在唤醒后 park 调用方 200ms——transport 保证
    // DATA(BYE_ACK) 先于 DISCONNECT 事件，park 窗口内 DISCONNECT 必然已被
    // server_loop 处理：修复前必红，修复后（ACK 到达处同线程标记）必绿。
    DisconnectLatch latch;
    client->set_disconnect_handler([&latch](uint64_t conn_id) { latch.notify(conn_id); });

    uint64_t conn = setup_connected_pair(
        [](uint64_t, uint64_t, uint64_t, const CMString&) {
            return std::optional<CMString>{"ack"};
        });
    ASSERT_NE(conn, 0u);
    // 先做一次 RPC 往返（连接进入活跃状态）。
    CallbackLatch rl;
    client->set_response_handler([&rl](uint64_t, uint64_t rpc_id, uint8_t st,
                                       const CMString& pl) { rl.notify(rpc_id, st, pl); });
    ASSERT_TRUE(client->send_request(conn, 1, 1, "warm"));
    ASSERT_TRUE(rl.wait());

    client->bye_wake_hook_for_testing_ = [](uint64_t, bool got_ack) {
        if (got_ack) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    };
    EXPECT_TRUE(client->send_bye(conn));
    EXPECT_FALSE(client->is_connected(conn));
    // park 窗口内 DISCONNECT 已被 server_loop 处理：不得误报断连。
    EXPECT_FALSE(latch.fired()) << "ACK 后的 DISCONNECT 竞态窗口内误报断连";
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(latch.fired());
}

TEST_F(PeerRpcServerTest, ConnectPeerRetriesThenFailsCleanly) {
    // 连接一个确定未监听的端口：retries 内仍失败 → 返回 0（不挂死）。
    uint64_t conn = client->connect_peer("127.0.0.1", /*port=*/1,
                                          /*retries=*/2, /*retry_interval_ms=*/50);
    EXPECT_EQ(conn, 0u);
}

TEST_F(PeerRpcServerTest, StopClosesAllConnections) {
    DisconnectLatch latch;
    client->set_disconnect_handler([&latch](uint64_t conn_id) { latch.notify(conn_id); });

    uint64_t conn = setup_connected_pair(
        [](uint64_t, uint64_t, uint64_t, const CMString&) {
            return std::optional<CMString>{"ack"};
        });
    ASSERT_NE(conn, 0u);
    EXPECT_TRUE(client->is_connected(conn));
    // 先做一次 RPC 往返：stop() 的 disconnect 通知只覆盖活跃连接
    //（recv_bufs_ 有条目者）——无数据往来的连接不在通知范围。
    CallbackLatch rl;
    client->set_response_handler([&rl](uint64_t, uint64_t rpc_id, uint8_t st,
                                       const CMString& pl) { rl.notify(rpc_id, st, pl); });
    ASSERT_TRUE(client->send_request(conn, 1, 1, "warm"));
    ASSERT_TRUE(rl.wait());

    client->stop();
    EXPECT_TRUE(latch.wait()) << "stop() must close connections and fire disconnect handlers";
    EXPECT_FALSE(client->is_connected(conn));
}

}  // namespace
}  // namespace fly
