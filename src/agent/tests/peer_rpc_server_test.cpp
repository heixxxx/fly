// PeerRpcServer 单元测试（2026-08-16 补覆盖：v2 daemon 通信底座此前零单测，
// 仅 qa/solver 的 v2 case 兜底）。真实 localhost TCP 验证：
//   listen 端口分配 / 端到端 RPC 往返 / 异步响应 / 失败通知 / BYE 优雅关闭 /
//   connect 重试 / stop 清理。

#include <gtest/gtest.h>
#include <agent/cpp/peer_rpc_server.h>
#include <agent/cpp/worker_agent.h>
#include <storage/cpp/pipeline.h>
#include <common/buffer/cpp/data_checksum.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

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

// 泛型值交接 latch（handler 存 / 测试线程取——server_loop 不得内联阻塞）。
template <typename T>
struct Latch {
    void store(T v) {
        std::lock_guard<std::mutex> lk(m_);
        v_ = std::move(v);
        cv_.notify_all();
    }
    T wait(int timeout_s = 5) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_for(lk, std::chrono::seconds(timeout_s),
                     [this] { return v_.has_value(); });
        return v_.value_or(T{});
    }
    std::optional<T> v_;
    std::mutex m_;
    std::condition_variable cv_;
};

// 流式读端交接：handler（server_loop 线程）只存 reader 立即返回；
// 消费在测试线程——handler 内联消费会阻塞 server_loop 喂数据（死锁）。
struct ReaderLatch {
    void store(PeerStreamReaderPtr r) {
        std::lock_guard<std::mutex> lk(m_);
        reader_ = std::move(r);
        cv_.notify_all();
    }
    PeerStreamReaderPtr wait(int timeout_s = 5) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_for(lk, std::chrono::seconds(timeout_s),
                     [this] { return reader_ != nullptr; });
        return reader_;
    }
    PeerStreamReaderPtr reader_;
    std::mutex m_;
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

    std::shared_ptr<PeerRpcServer> server = std::make_shared<PeerRpcServer>();
    std::shared_ptr<PeerRpcServer> client = std::make_shared<PeerRpcServer>();
};

TEST(PeerStreamReaderTest, SingleBlockFileProtocolReadsAcrossCursor) {
    // 单块读端全协议：游标推进 / readinto 直填 / readline 按行 / EOF 收敛。
    // 长度构造：嵌 NUL 的二进制数据不能走 strlen ctor（会在 \x00 截断）。
    const char* kRaw = "hello world\nsecond line\n\xff\x00" "binary";
    PeerStreamReader r(CMString(kRaw, 32));

    EXPECT_EQ(r.read(5), CMString("hello"));
    EXPECT_EQ(r.readline(), CMString(" world\n"));

    char dst[8] = {};
    EXPECT_EQ(r.readinto(dst, 8), 8u);   // "second l"
    EXPECT_EQ(CMString(dst, 8), "second l");

    // 新读端重读（单块无游标回退 API——顺序消费语义）。
    PeerStreamReader r2(CMString(kRaw, 32));
    EXPECT_EQ(r2.read(100), CMString(kRaw, 32));
    EXPECT_EQ(r2.read(10), CMString());   // EOF 收敛
    EXPECT_FALSE(r.failed());
}

TEST(PeerStreamReaderTest, SingleBlockMovePreservesData) {
    PeerStreamReader r(CMString("payload-abc"));
    EXPECT_EQ(r.read_all(), CMString("payload-abc"));
    EXPECT_FALSE(r.failed());
}

TEST_F(PeerRpcServerTest, ListenAllocatesPortAndStopCleansUp) {
    EXPECT_FALSE(server->is_running());
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&, const PeerStreamReaderPtr&) {
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
    ReaderLatch rlatch;
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString&,
            const PeerStreamReaderPtr& reader) {
            rlatch.store(reader);   // START 即派发：handler 只交接读端
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    PeerStreamWriter w(client, conn, /*rpc_id=*/9, /*direction=*/0,
                       CompressionType::LZ4, -1);
    ASSERT_TRUE(w.ok());
    w.write(payload.data(), payload.size());
    ASSERT_TRUE(w.finish());
    EXPECT_EQ(w.total_uncompressed(), payload.size());

    // 测试线程消费读端（pickle.load 同款拉动语义；EOF = END 对账通过）
    auto reader = rlatch.wait();
    ASSERT_TRUE(reader != nullptr);
    const CMString data = reader->read_all();
    EXPECT_EQ(data.size(), payload.size());
    EXPECT_EQ(data_checksum(data.data(), data.size()), expect_crc);
    EXPECT_FALSE(reader->failed());
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
    ReaderLatch rlatch;
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString&,
            const PeerStreamReaderPtr& reader) {
            rlatch.store(reader);
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    PeerStreamWriter w(client, conn, /*rpc_id=*/11, /*direction=*/0,
                       CompressionType::LZ4, -1);
    ASSERT_TRUE(w.ok());
    for (size_t off = 0; off < payload.size(); off += 7 * 1024) {
        const size_t n = std::min<size_t>(7 * 1024, payload.size() - off);
        w.write(payload.data() + off, n);
    }
    ASSERT_TRUE(w.finish());
    EXPECT_EQ(w.total_uncompressed(), payload.size());

    auto reader = rlatch.wait();
    ASSERT_TRUE(reader != nullptr);
    // 小增量逐段读（跨 chunk/跨块游标路径）。
    CMString data;
    char buf[4096];
    while (true) {
        const size_t got = reader->readinto(buf, sizeof(buf));
        if (got == 0) break;
        data.append(buf, got);
    }
    EXPECT_EQ(data.size(), payload.size());
    EXPECT_EQ(data_checksum(data.data(), data.size()), expect_crc);
    EXPECT_FALSE(reader->failed());
}

TEST_F(PeerRpcServerTest, StreamResponseRoundtripSmallPayload) {
    // 统一流式协议：小 payload（16B，单块流）请求 → 流式响应 echo。
    // START/DATA/END 三帧合并语义下小消息同样走管线（无阈值裁定）。
    const std::string payload = MakePseudoRandomBytes(16);
    CallbackLatch latch;
    ReaderLatch rlatch;
    client->set_response_handler([&latch, &rlatch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload_in,
                                          const PeerStreamReaderPtr& reader) {
        if (reader) rlatch.store(reader);   // 流式响应：reader 承载
        latch.notify(rpc_id, status, payload_in);
    });

    // echo 作业交接：请求 reader 由 handler 存下（server_loop 线程不得
    // 内联消费——会阻塞喂数据），测试线程消费后回响应。
    struct EchoJob {
        uint64_t c = 0;
        uint64_t rid = 0;
        PeerStreamReaderPtr reader;
    };
    auto job_latch = std::make_shared<Latch<EchoJob>>();
    uint64_t conn = setup_connected_pair(
        [&](uint64_t c, uint64_t rid, uint64_t, const CMString&,
            const PeerStreamReaderPtr& reader) {
            job_latch->store({c, rid, reader});
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    PeerStreamWriter w(client, conn, /*rpc_id=*/77, /*direction=*/0,
                       CompressionType::LZ4, -1);
    ASSERT_TRUE(w.ok());
    w.write(payload.data(), payload.size());
    ASSERT_TRUE(w.finish());

    // 测试线程：消费请求读端（拉满 = END 对账通过）→ 回流式响应。
    const EchoJob job = job_latch->wait();
    ASSERT_TRUE(job.reader != nullptr);
    EXPECT_EQ(job.reader->read_all(), payload);   // 流式请求经读端到达
    uint64_t total = 0;
    uint32_t chunks = 0;
    EXPECT_TRUE(server->send_stream_payload(job.c, job.rid, /*direction=*/1,
                                            payload, CompressionType::LZ4, -1,
                                            total, chunks));

    ASSERT_TRUE(latch.wait()) << "streamed response should arrive";
    EXPECT_EQ(latch.rpc_id_, 77u);
    EXPECT_EQ(latch.status_, static_cast<uint8_t>(PeerRpcWireStatus::OK));
    auto reader = rlatch.wait();
    ASSERT_TRUE(reader != nullptr);
    EXPECT_EQ(reader->read_all(), payload);
}

TEST_F(PeerRpcServerTest, StreamTruncatedVerifyClosesConnection) {
    // END 对账失配：零容忍——坏流不交付（handler 不触发），server 主动
    // close 使 client 侧收到 DISCONNECT（调用方等待语义由 DISCONNECT 兜底）。
    // shared 持有：server close 引发的 client DISCONNECT 可能在测试函数
    // 返回（TearDown stop）之后到达，handler 不得引用栈上对象。
    auto dlatch = std::make_shared<DisconnectLatch>();
    client->set_disconnect_handler([dlatch](uint64_t c) { dlatch->notify(c); });
    ReaderLatch rlatch;
    std::atomic<bool> handler_fired{false};
    uint64_t conn = setup_connected_pair([&](uint64_t, uint64_t, uint64_t,
                                             const CMString&,
                                             const PeerStreamReaderPtr& reader) {
        handler_fired = true;
        rlatch.store(reader);
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
    EXPECT_TRUE(handler_fired.load()) << "START 即派发读端（零容忍移到读端）";
    // 读端消费触发对账：谎报 total → 读操作抛错（零容忍不静默半交付）。
    auto reader = rlatch.wait();
    ASSERT_TRUE(reader != nullptr);
    bool threw = false;
    try {
        reader->read_all();
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "对账失配必须以异常暴露";
    EXPECT_TRUE(reader->failed());
    // 终态语义：END 已处理后流已终结——读端抛错即零容忍暴露点
    // （流中途判死时 feed 停止 → 断连路径同样成立）。
}

TEST_F(PeerRpcServerTest, EndToEndRoundTrip) {
    CallbackLatch latch;
    client->set_response_handler([&latch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload, const PeerStreamReaderPtr&) {
        latch.notify(rpc_id, status, payload);
    });

    uint64_t conn = setup_connected_pair(
        [](uint64_t, uint64_t, uint64_t src, const CMString& payload, const PeerStreamReaderPtr&) {
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
                                          const CMString& payload, const PeerStreamReaderPtr&) {
        latch.notify(rpc_id, status, payload);
    });

    uint64_t server_conn = 0;
    uint64_t conn = setup_connected_pair(
        [&server_conn](uint64_t conn_id, uint64_t rpc_id, uint64_t, const CMString&, const PeerStreamReaderPtr&) {
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
                                          const CMString& payload, const PeerStreamReaderPtr&) {
        latch.notify(rpc_id, status, payload);
    });

    // 服务端 handler 直接 notify 失败。
    uint64_t server_conn = 0;
    uint64_t conn = setup_connected_pair(
        [&server_conn, this](uint64_t conn_id, uint64_t, uint64_t, const CMString&, const PeerStreamReaderPtr&) {
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
        [](uint64_t, uint64_t, uint64_t, const CMString&, const PeerStreamReaderPtr&) {
            return std::optional<CMString>{"ack"};
        });
    ASSERT_NE(conn, 0u);
    // 先做一次 RPC 往返（连接进入活跃状态，排除 recv_bufs_ 未建条目的干扰）。
    CallbackLatch rl;
    client->set_response_handler([&rl](uint64_t, uint64_t rpc_id, uint8_t st,
                                       const CMString& pl, const PeerStreamReaderPtr& reader) {
            rl.notify(rpc_id, st, pl);
        });
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
        [](uint64_t, uint64_t, uint64_t, const CMString&, const PeerStreamReaderPtr&) {
            return std::optional<CMString>{"ack"};
        });
    ASSERT_NE(conn, 0u);
    // 先做一次 RPC 往返（连接进入活跃状态）。
    CallbackLatch rl;
    client->set_response_handler([&rl](uint64_t, uint64_t rpc_id, uint8_t st,
                                       const CMString& pl, const PeerStreamReaderPtr& reader) {
            rl.notify(rpc_id, st, pl);
        });
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
        [](uint64_t, uint64_t, uint64_t, const CMString&, const PeerStreamReaderPtr&) {
            return std::optional<CMString>{"ack"};
        });
    ASSERT_NE(conn, 0u);
    EXPECT_TRUE(client->is_connected(conn));
    // 先做一次 RPC 往返：stop() 的 disconnect 通知只覆盖活跃连接
    //（recv_bufs_ 有条目者）——无数据往来的连接不在通知范围。
    CallbackLatch rl;
    client->set_response_handler([&rl](uint64_t, uint64_t rpc_id, uint8_t st,
                                       const CMString& pl, const PeerStreamReaderPtr& reader) {
            rl.notify(rpc_id, st, pl);
        });
    ASSERT_TRUE(client->send_request(conn, 1, 1, "warm"));
    ASSERT_TRUE(rl.wait());

    client->stop();
    EXPECT_TRUE(latch.wait()) << "stop() must close connections and fire disconnect handlers";
    EXPECT_FALSE(client->is_connected(conn));
}

// 原始连接工厂：绕过 PeerRpcServer 客户端封装，直发任意字节（畸形帧注入）。
static std::pair<CMUniquePtr<ConnectionManager>, uint64_t> connect_raw(int port) {
    auto raw = create_connection_manager("tcp");
    if (!raw) return {nullptr, 0};
    uint64_t conn = raw->connect("127.0.0.1", port);
    if (conn == 0) return {nullptr, 0};
    return {std::move(raw), conn};
}

// 轮询连接关闭（deadline 模式，零容忍断流的可观察信号）。poll 驱动
// DISCONNECT 事件处理（is_connected 状态由事件循环更新）。
static bool wait_closed(ConnectionManager& raw, uint64_t conn, int timeout_ms = 5000) {
    for (int i = 0; i < timeout_ms / 20; i++) {
        raw.poll(0);
        if (!raw.is_connected(conn)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    raw.poll(0);
    return !raw.is_connected(conn);
}

TEST_F(PeerRpcServerTest, MalformedShortTotalLenClosesInsteadOfCrash) {
    // RESPONSE total_len=1（< fixed-8=10）+ 24B 粘包填充：修复前
    // payload_len = 8+1-18 在 size_t 下溢为巨值 → CMString 巨值构造 →
    // length_error → server_loop 无 catch → std::terminate 全进程崩溃。
    // 修复后：下限校验 → 零容忍断流（连接关闭，进程存活）。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&,
                                 const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    CMString frame;
    frame.resize(9 + 24);   // 9B 头 + 1B type + 24B 粘包（凑足 fixed=18 判定）
    write_be64(frame.data(), make_frame_header(1));   // total_len=1：非法短帧
    frame[8] = static_cast<char>(static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE));
    ASSERT_TRUE(raw->send(rc, frame) > 0);
    EXPECT_TRUE(wait_closed(*raw, rc))
        << "undersized total_len must trigger zero-tolerance close (not crash)";
    raw->close_all();
}

TEST_F(PeerRpcServerTest, UnknownFrameTypeClosesConnection) {
    // 封闭协议下未知帧类型 = 帧错位：clear 会丢同批粘包好帧且不判死
    // （读端只能等 CRC 失败兜底或永久等 END）——必须零容忍断流。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&,
                                 const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    CMString frame;
    frame.resize(9 + 4);
    write_be64(frame.data(), make_frame_header(4));
    frame[8] = 0x7F;   // 未定义帧类型
    ASSERT_TRUE(raw->send(rc, frame) > 0);
    EXPECT_TRUE(wait_closed(*raw, rc))
        << "unknown frame type must close the connection (no silent stall)";
    raw->close_all();
}

TEST_F(PeerRpcServerTest, StopUnblocksBackpressuredFeed) {
    // 满队列背压（块记录 > 剩余上界，feed 永久 wait）+ 读端不消费：
    // stop 必须先置 failed 唤醒 feed 再 join——顺序颠倒则 join 永不返回
    // （stop 挂死回归测试）。
    PeerStreamRxState::kQueueBytes = 5 * 1024 * 1024;   // 注入小上界（确定性满队列）
    struct QueueBytesGuard {
        ~QueueBytesGuard() { PeerStreamRxState::kQueueBytes = 64 * 1024 * 1024; }
    } guard;
    ReaderLatch rlatch;
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString&,
            const PeerStreamReaderPtr& reader) {
            rlatch.store(reader);
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    // 7MB 伪随机（lz4 近随机 → raw 直通，压缩态≈7MB）：块1 4MB 入队
    // （4≤5）；块2 3MB：4+3>5 → feed 永久 wait（读端不消费不释放）。
    const std::string payload = MakePseudoRandomBytes(7 * 1024 * 1024);
    PeerStreamWriter w(client, conn, /*rpc_id=*/21, /*direction=*/0,
                       CompressionType::LZ4, -1);
    ASSERT_TRUE(w.ok());
    std::thread wt([&] {
        w.write(payload.data(), payload.size());
        w.finish();
    });
    auto reader = rlatch.wait();   // START 即派发
    ASSERT_TRUE(reader != nullptr);

    // 确定性前置：网络线程已阻塞在满队列 feed wait。
    bool blocked = false;
    for (int i = 0; i < 250 && !blocked; i++) {
        blocked = server->feed_blocked_for_testing_.load() > 0;
        if (!blocked) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(blocked) << "precondition: feed must be blocked on full queue";

    auto fut = std::async(std::launch::async, [&] { server->stop(); });
    EXPECT_EQ(fut.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "stop() must not hang when server_loop is blocked on backpressured feed";
    wt.join();
}

TEST_F(PeerRpcServerTest, ReaderVerifyFailureWakesBlockedFeed) {
    // 读端 CRC 失配置 failed 时网络线程可能正阻塞在满队列 feed wait——
    // 漏 notify 则 server_loop 整线程冻结（所有连接事件停摆）。
    PeerStreamRxState::kQueueBytes = 1500 * 1024;   // 1.5MB：块0(1MB) 可入队，块1 阻塞
    struct QueueBytesGuard {
        ~QueueBytesGuard() { PeerStreamRxState::kQueueBytes = 64 * 1024 * 1024; }
    } guard;
    ReaderLatch rlatch;
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString&,
            const PeerStreamReaderPtr& reader) {
            rlatch.store(reader);
            return std::nullopt;
        });
    ASSERT_NE(conn, 0u);

    // 手工构造 4 块 NONE 块流（每块 1MB），翻转块0 的 CRC —— 读端消费块0
    // 即 CrcVerify 失败 → advance_block 置 failed（此时 feed 阻塞在块1）。
    EXPECT_TRUE(client->send_stream_start(conn, 33, 0,
                                          static_cast<uint8_t>(CompressionType::NONE)));
    constexpr size_t kBlock = 1024 * 1024;
    CMString block_stream;
    {
        fly::EmitFn emit = [&](const char* d, size_t n) { block_stream.append(d, n); };
        fly::WritePipeline wp(
            fly::make_file_write_pipeline(nullptr, kBlock, emit, 85, -1));
        const std::string data = MakePseudoRandomBytes(4 * kBlock);
        wp.write(data.data(), data.size());
        wp.finish();
    }
    const size_t b0 = 8;   // 块0 头布局 [4B unc][4B comp][8B crc]
    for (size_t i = 0; i < 8; i++) block_stream[b0 + i] = static_cast<char>(~block_stream[b0 + i]);
    EXPECT_TRUE(client->send_stream_data(conn, block_stream.data(),
                                         block_stream.size()));
    // 不发 END：让 feed 阻塞在块1（1MB+16B 后 1+1 > 1.5MB 上界）成为稳定态。

    auto reader = rlatch.wait();
    ASSERT_TRUE(reader != nullptr);
    // 等前置：feed 已阻塞在满队列（块1 无法入队）。
    bool blocked = false;
    for (int i = 0; i < 250 && !blocked; i++) {
        blocked = server->feed_blocked_for_testing_.load() > 0;
        if (!blocked) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(blocked) << "precondition: feed blocked on block 1";
    // 读端消费块0 → CRC 失败 → 置 failed 并必须唤醒 feed（漏 notify =
    // server_loop 冻结，feed_blocked 计数永不回落）。
    bool threw = false;
    try {
        reader->read(1);
    } catch (const std::exception&) {
        threw = true;
    }
    EXPECT_TRUE(threw) << "corrupted block must surface as exception";
    bool woke = false;
    for (int i = 0; i < 250 && !woke; i++) {
        woke = server->feed_blocked_for_testing_.load() == 0;
        if (!woke) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(woke) << "reader verify failure must notify the blocked feed "
                         "(server_loop freeze otherwise)";
}

// ── 故障帧族（零容忍语义逐分支覆盖）────────────────────────────────
// 手法同 MalformedShortTotalLen：裸 ConnectionManager 对在听 server 直发
// 任意帧。断言统一为「确定性可观察终态」（close 或静默丢弃后的帧同步），
// 全程不得 crash/terminate。

// 通用帧装配：[8B frame header][type][payload]（total_len = 1 + payload）。
static CMString make_raw_frame(uint8_t type, const CMString& payload) {
    CMString frame;
    frame.resize(9 + payload.size());
    write_be64(frame.data(), make_frame_header(1 + payload.size()));
    frame[8] = static_cast<char>(type);
    if (!payload.empty()) {
        std::memcpy(frame.data() + 9, payload.data(), payload.size());
    }
    return frame;
}

// DATA_CHUNK 帧：[8B hdr][1B type][4B small_len=16][16B offset+crc][raw]。
static CMString make_data_chunk_frame(const CMString& raw) {
    CMString payload;
    payload.resize(4 + 16, '\0');
    write_be32(payload.data(), ChunkFrameProtocol::kSmallFieldsLen);
    // offset/crc 保持 0（接收端不校验帧级 crc——0 = 发送端未计算）。
    payload.append(raw);
    return make_raw_frame(static_cast<uint8_t>(MessageType::DATA_CHUNK), payload);
}

TEST_F(PeerRpcServerTest, CorruptStreamStartClosesConnection) {
    // START 帧头合法但 payload 截断（bitsery 读越界 → decode 失败）：
    // 流式零容忍 → 连接 close，进程存活。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&,
                                 const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    CMString garbage(4, '\xFF');   // 远小于 START 的 bitsery 字段需求
    ASSERT_TRUE(raw->send(rc, make_raw_frame(
        static_cast<uint8_t>(MessageType::PEER_STREAM_START), garbage)) > 0);
    EXPECT_TRUE(wait_closed(*raw, rc))
        << "corrupt START must trigger zero-tolerance close (not crash)";
    raw->close_all();
}

TEST_F(PeerRpcServerTest, SecondStartWhileStreamActiveFailsOldStreamAndKeepsConn) {
    // 同连接旧流未收尾再 START：协议错位判死旧流（reader 置 failed），
    // 新流照常建立、连接保持——帧失步的旧流不得拖死连接上的新流。
    std::mutex m;
    std::condition_variable cv;
    CMVector<PeerStreamReaderPtr> readers;   // 按 START 到达序
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString& payload,
            const PeerStreamReaderPtr& reader) -> std::optional<CMString> {
            if (reader) {
                std::lock_guard<std::mutex> lk(m);
                readers.push_back(reader);
                cv.notify_all();
                return std::nullopt;
            }
            return std::optional<CMString>{"echo:" + payload};
        });
    ASSERT_NE(conn, 0u);

    EXPECT_TRUE(client->send_stream_start(conn, /*rpc_id=*/5, /*direction=*/0,
                                          static_cast<uint8_t>(CompressionType::NONE)));
    bool got_first = false;
    for (int i = 0; i < 100 && !got_first; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::lock_guard<std::mutex> lk(m);
        got_first = readers.size() >= 1;
    }
    ASSERT_TRUE(got_first) << "first START must dispatch its reader";

    // 旧流（rpc 5）未收尾直接开新流（rpc 6）：旧 reader 必须被判死。
    EXPECT_TRUE(client->send_stream_start(conn, /*rpc_id=*/6, /*direction=*/0,
                                          static_cast<uint8_t>(CompressionType::NONE)));
    bool got_second = false;
    for (int i = 0; i < 100 && !got_second; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::lock_guard<std::mutex> lk(m);
        got_second = readers.size() >= 2;
    }
    ASSERT_TRUE(got_second) << "second START must dispatch a fresh reader";
    PeerStreamReaderPtr first, second;
    {
        std::lock_guard<std::mutex> lk(m);
        first = readers[0];
        second = readers[1];
    }
    ASSERT_TRUE(first != nullptr);
    ASSERT_TRUE(second != nullptr);
    EXPECT_TRUE(first->failed())
        << "the unterminated old stream must be failed by the new START";
    EXPECT_TRUE(client->is_connected(conn))
        << "a protocol-misordered START must not kill the connection";

    // 新流照常工作：合法块流 + 如实对账 END → 读端完整收齐。
    const std::string data = MakePseudoRandomBytes(4096);
    CMString block_stream;
    {
        fly::EmitFn emit = [&](const char* d, size_t n) { block_stream.append(d, n); };
        fly::WritePipeline wp(
            fly::make_file_write_pipeline(nullptr, 32 * 1024, emit, 85, -1));
        wp.write(data.data(), data.size());
        wp.finish();
    }
    EXPECT_TRUE(client->send_stream_data(conn, block_stream.data(), block_stream.size()));
    EXPECT_TRUE(client->send_stream_end(conn, /*rpc_id=*/6,
                                        /*total_uncompressed=*/data.size(),
                                        /*chunk_count=*/1,
                                        /*consumed=*/block_stream.size()));
    // read_all 拉至 EOF（END 对账通过才放行）——与 StreamRequestLargePayload
    // 同款消费语义；失败/失配以异常或 failed() 暴露。
    const CMString got = second->read_all();
    EXPECT_EQ(got.size(), data.size()) << "the new stream must deliver intact";
    EXPECT_FALSE(second->failed()) << "the new stream must stay healthy";
    EXPECT_TRUE(client->is_connected(conn));
}

TEST_F(PeerRpcServerTest, CorruptStreamEndClosesConnection) {
    // 合法 START 后接坏 END（payload 截断 → decode 失败）：零容忍 close。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&,
                                 const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    // START 经裸连接直发（client 封装没有这条连接）。
    PeerStreamStartMessage start;
    start.rpc_id_ = 7;
    start.direction_ = 0;
    start.compression_type_ = static_cast<uint8_t>(CompressionType::NONE);
    ASSERT_TRUE(raw->send(rc, MessageProtocol::encode(start)) > 0);
    CMString garbage(3, '\xFF');
    ASSERT_TRUE(raw->send(rc, make_raw_frame(
        static_cast<uint8_t>(MessageType::PEER_STREAM_END), garbage)) > 0);
    EXPECT_TRUE(wait_closed(*raw, rc))
        << "corrupt END must trigger zero-tolerance close (not crash)";
    raw->close_all();
}

TEST_F(PeerRpcServerTest, EndWithoutActiveStreamClosesConnection) {
    // 无流上下文的合法 END（decode 通过、streams_ 无条目）：协议错位 → close。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&,
                                 const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    PeerStreamEndMessage end;   // 合法编码（无流上下文才是被测分支）
    ASSERT_TRUE(raw->send(rc, MessageProtocol::encode(end)) > 0);
    EXPECT_TRUE(wait_closed(*raw, rc))
        << "END without an active stream must close the connection (no silent stall)";
    raw->close_all();
}

TEST_F(PeerRpcServerTest, DataWithoutActiveStreamDiscardedKeepsFrameSync) {
    // 无流 DATA_CHUNK：整帧丢弃（WARN）+ 连接保持——丢弃必须保持帧同步，
    // 后续合法请求照常往返（clear 丢同批好帧是零容忍反例）。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString& payload,
                                 const PeerStreamReaderPtr&) {
                                  return std::optional<CMString>{"echo:" + payload};
                              });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    ASSERT_TRUE(raw->send(rc, make_data_chunk_frame("orphan-block-bytes")) > 0);

    // 前置：丢弃发生（100ms 足够本机回环；precondition 等待而非结果断言）。
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(raw->is_connected(rc))
        << "orphan DATA must be discarded, not fatal";

    // 帧同步证据：同一连接上的合法 REQUEST 响应原样读回。
    CMString body;
    body.resize(16, '\0');
    write_be64(body.data(), /*rpc_id=*/33);
    write_be64(body.data() + 8, /*src=*/7);
    body.append("after-orphan");
    ASSERT_TRUE(raw->send(rc, make_raw_frame(
        static_cast<uint8_t>(MessageType::PEER_RPC_REQUEST), body)) > 0);
    bool got = false;
    for (int i = 0; i < 150 && !got; ++i) {
        for (const auto& e : raw->poll(0)) {
            if (e.type_ == TransportEventType::DATA && e.data_.size() >= 18 &&
                static_cast<uint8_t>(e.data_[8]) ==
                    static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE) &&
                read_be64(e.data_.data() + 9) == 33) {
                got = true;
            }
        }
        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(got) << "frame sync must survive an orphan DATA_CHUNK discard";
    EXPECT_TRUE(raw->is_connected(rc));
    raw->close_all();
}

TEST_F(PeerRpcServerTest, DataChunkZeroRawLenOnActiveStreamClearsAndContinues) {
    // 活跃流上的 DATA_CHUNK zero raw_len（total_len=21 = 头 21B、无 raw）。
    // 当前实现语义：只清接收缓冲防积压（buf.clear + break），不判流死、
    // 不判连死——与 corrupt START/END 的零容忍 close 是不同分支。断言：
    // 不 crash、流上下文存活（后续合法 END 被正常消费而非判死）、连接保持、
    // 同连接后续请求照常往返（帧同步恢复）。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString& payload,
                                 const PeerStreamReaderPtr&) -> std::optional<CMString> {
                                  return std::optional<CMString>{"echo:" + payload};
                              });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    // START 经裸连接直发（client 封装没有这条连接）。
    PeerStreamStartMessage start;
    start.rpc_id_ = 8;
    start.direction_ = 0;
    start.compression_type_ = static_cast<uint8_t>(CompressionType::NONE);
    ASSERT_TRUE(raw->send(rc, MessageProtocol::encode(start)) > 0);
    // total_len = 1 + 4 + 16 = 21 → raw_len = 0。
    CMString empty_small(20, '\0');
    write_be32(empty_small.data(), ChunkFrameProtocol::kSmallFieldsLen);
    ASSERT_TRUE(raw->send(rc, make_raw_frame(
        static_cast<uint8_t>(MessageType::DATA_CHUNK), empty_small)) > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(raw->is_connected(rc))
        << "zero raw_len must not crash (buffer cleared, conn kept per current contract)";

    // 流上下文存活：合法 END 被当作活跃流的收尾正常消费（不触发
    // "END without active stream" 判死），连接保持。
    PeerStreamEndMessage end;
    end.rpc_id_ = 8;
    ASSERT_TRUE(raw->send(rc, MessageProtocol::encode(end)) > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(raw->is_connected(rc))
        << "the active stream must have consumed the legitimate END";

    // 帧同步恢复：缓冲清除后的合法请求照常往返。
    CMString body;
    body.resize(16, '\0');
    write_be64(body.data(), /*rpc_id=*/34);
    write_be64(body.data() + 8, /*src=*/7);
    body.append("after-zero-raw");
    ASSERT_TRUE(raw->send(rc, make_raw_frame(
        static_cast<uint8_t>(MessageType::PEER_RPC_REQUEST), body)) > 0);
    bool got = false;
    for (int i = 0; i < 150 && !got; ++i) {
        for (const auto& e : raw->poll(0)) {
            if (e.type_ == TransportEventType::DATA && e.data_.size() >= 18 &&
                static_cast<uint8_t>(e.data_[8]) ==
                    static_cast<uint8_t>(MessageType::PEER_RPC_RESPONSE) &&
                read_be64(e.data_.data() + 9) == 34) {
                got = true;
            }
        }
        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_TRUE(got) << "frame sync must survive the zero raw_len discard";
    EXPECT_TRUE(raw->is_connected(rc));
    raw->close_all();
}

TEST_F(PeerRpcServerTest, BadFrameHeaderClearsBufferNoCrash) {
    // 非法帧头（total_len=0，header check 失配）：清缓冲防积压但不判死——
    // 连接保持且后续合法帧照常处理（与 undersized total_len 的判死语义区分：
    // 那是帧完整到达后的类型级下溢，此处是头都解析不出的垃圾前缀）。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString& payload,
                                 const PeerStreamReaderPtr&) {
                                  return std::optional<CMString>{"echo:" + payload};
                              });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    CMString zeros(8, '\0');   // len=0 → parse_frame_header 失败 → get_total_size=0
    ASSERT_TRUE(raw->send(rc, zeros) > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(raw->is_connected(rc))
        << "unparseable 8B garbage must clear the buffer, not kill the connection";

    // 缓冲已清 + 帧同步：合法请求照常往返。
    // （裸 conn 无 response 回调——用 PeerRpcServer client 建第二条连接验证
    // server 存活即可；本连接的往返由下方 raw END 收尾不挂验证。）
    uint64_t probe = client->connect_peer("127.0.0.1", port);
    ASSERT_NE(probe, 0u);
    CallbackLatch latch;
    client->set_response_handler([&latch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload,
                                          const PeerStreamReaderPtr&) {
        latch.notify(rpc_id, status, payload);
    });
    EXPECT_TRUE(client->send_request(probe, /*rpc_id=*/42, 1, "after-garbage"));
    EXPECT_TRUE(latch.wait()) << "server must keep serving after a bad header";
    EXPECT_EQ(latch.payload_, "echo:after-garbage");
    raw->close_all();
}

TEST_F(PeerRpcServerTest, ListenFailurePaths) {
    // 已在听重复 listen → 0；端口被占 → 0（均不 crash、原实例不受影响）。
    int port = server->listen("127.0.0.1", 0,
                              [](uint64_t, uint64_t, uint64_t, const CMString&,
                                 const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port, 0);
    EXPECT_EQ(server->listen("127.0.0.1", 0,
                             [](uint64_t, uint64_t, uint64_t, const CMString&,
                                const PeerStreamReaderPtr&) { return std::nullopt; }), 0)
        << "double listen must fail cleanly";

    // 占用端口的裸 listener。
    int busy = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(busy, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASSERT_EQ(::bind(busy, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(busy, 4), 0);
    socklen_t len = sizeof(addr);
    ASSERT_EQ(::getsockname(busy, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    uint16_t busy_port = ntohs(addr.sin_port);

    PeerRpcServer other;
    EXPECT_EQ(other.listen("127.0.0.1", busy_port,
                           [](uint64_t, uint64_t, uint64_t, const CMString&,
                              const PeerStreamReaderPtr&) { return std::nullopt; }), 0)
        << "listen on a busy port must fail cleanly";
    EXPECT_FALSE(other.is_running());
    ::close(busy);
    EXPECT_TRUE(server->is_running()) << "the original listener must be unaffected";
}

TEST_F(PeerRpcServerTest, InvalidConstructionAndClosedConnNoOps) {
    // srv 为空 / conn_id=0：invalid 构造（ok=false，不 crash）。
    CMSharedPtr<PeerRpcServer> null_srv;
    PeerStreamWriter bad1(null_srv, 1, 1, 0, CompressionType::NONE, -1);
    EXPECT_FALSE(bad1.ok());
    auto srv = std::make_shared<PeerRpcServer>();
    PeerStreamWriter bad2(srv, /*conn_id=*/0, 1, 0, CompressionType::NONE, -1);
    EXPECT_FALSE(bad2.ok());
    bad1.write("x", 1);   // write no-op
    EXPECT_FALSE(bad1.finish());

    // 对已关连接构造：START 发送失败 → write no-op / finish false（不挂）。
    int port = srv->listen("127.0.0.1", 0,
                           [](uint64_t, uint64_t, uint64_t, const CMString&,
                              const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port, 0);
    auto [raw, rc] = connect_raw(port);
    ASSERT_TRUE(raw != nullptr);
    raw->close_all();
    // 前置：server 侧 transport 已感知 FIN（conn 已从其表移除）——START 的
    // send 失败才有确定性。
    bool reaped = false;
    for (int i = 0; i < 250 && !reaped; ++i) {
        reaped = !srv->is_connected(rc);
        if (!reaped) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(reaped) << "server must observe the closed peer conn";
    PeerStreamWriter dead(srv, rc, 9, 0, CompressionType::NONE, -1);
    EXPECT_FALSE(dead.ok()) << "START on a closed conn must fail construction";
    dead.write("data", 4);   // no-op：不得崩溃/阻塞
    EXPECT_FALSE(dead.finish());

    // 真实连接上不 finish 直接析构：压缩线程收尾 + END，不得挂死。
    // （writer 绑定 client 端——conn id 即 client 侧连接 id，按构造正确对应；
    //   server 端收到 START/数据/END 但无消费者，走 abandoned 弃投递，不 crash。）
    PeerRpcServer srv2;
    int port2 = srv2.listen("127.0.0.1", 0,
                            [](uint64_t, uint64_t, uint64_t, const CMString&,
                               const PeerStreamReaderPtr&) { return std::nullopt; });
    ASSERT_GT(port2, 0);
    uint64_t cc = client->connect_peer("127.0.0.1", port2);
    ASSERT_NE(cc, 0u);
    {
        PeerStreamWriter abandoned(client, cc, 10, 0, CompressionType::NONE, -1);
        ASSERT_TRUE(abandoned.ok());
        abandoned.write("no-finish-payload", 17);
    }   // 析构：close 队列 → 压缩线程排空 + 发尾块 + END → join
    raw->close_all();
    srv2.stop();
}

TEST_F(PeerRpcServerTest, AbandonedReaderDiscardsAndKeepsFrameSync) {
    // 读端析构（abandoned）后继续到来的 DATA/END：网络线程弃记录但保持帧
    // 同步至 END——不阻塞、不 crash，连接上后续请求照常往返。
    std::mutex m;
    std::condition_variable cv;
    PeerStreamReaderPtr reader;
    CallbackLatch latch;
    client->set_response_handler([&latch](uint64_t, uint64_t rpc_id, uint8_t status,
                                          const CMString& payload,
                                          const PeerStreamReaderPtr&) {
        latch.notify(rpc_id, status, payload);
    });
    uint64_t conn = setup_connected_pair(
        [&](uint64_t, uint64_t, uint64_t, const CMString& payload,
            const PeerStreamReaderPtr& r) -> std::optional<CMString> {
            if (r) {
                std::lock_guard<std::mutex> lk(m);
                reader = r;
                cv.notify_all();
                return std::nullopt;
            }
            return std::optional<CMString>{"echo:" + payload};
        });
    ASSERT_NE(conn, 0u);

    EXPECT_TRUE(client->send_stream_start(conn, 21, 0,
                                          static_cast<uint8_t>(CompressionType::NONE)));
    {
        std::unique_lock<std::mutex> lk(m);
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(5),
                                [&] { return reader != nullptr; }));
    }
    reader.reset();   // 中途废弃：abandoned 置位（网络线程弃投递）

    // 继续发完整流（DATA + 如实 END）：不阻塞不 crash。
    const std::string data = MakePseudoRandomBytes(8192);
    CMString block_stream;
    {
        fly::EmitFn emit = [&](const char* d, size_t n) { block_stream.append(d, n); };
        fly::WritePipeline wp(
            fly::make_file_write_pipeline(nullptr, 32 * 1024, emit, 85, -1));
        wp.write(data.data(), data.size());
        wp.finish();
    }
    EXPECT_TRUE(client->send_stream_data(conn, block_stream.data(), block_stream.size()));
    EXPECT_TRUE(client->send_stream_end(conn, 21, data.size(), 1, block_stream.size()));

    // 帧同步保持：同连接后续单帧请求照常往返。
    EXPECT_TRUE(client->send_request(conn, /*rpc_id=*/22, 1, "after-abandon"));
    EXPECT_TRUE(latch.wait()) << "connection must keep serving after an abandoned reader";
    EXPECT_EQ(latch.payload_, "echo:after-abandon");
    EXPECT_TRUE(client->is_connected(conn));
}

// ── WorkerAgent 级 NOT_READY / FAILED 全链（PeerChannelGroup 底座语义）──
// 线上 NOT_READY 是「可恢复未就绪」一等状态：server peer_rpc_respond_not_ready
// → client peer_rpc_call 收到 NOT_READY（不判死）；对端整体 stop_peer_rpc →
// pending/新 call 收 FAILED；无应答小 timeout → FAILED("timeout")。
TEST(WorkerAgentPeerRpcTest, NotReadyFailureAndTimeoutPaths) {
    WorkerAgent server_w(1, "127.0.0.1", 0);
    WorkerAgent client_w(2, "127.0.0.1", 0);
    const int port = server_w.start_peer_rpc_listen("127.0.0.1", 0);
    ASSERT_GT(port, 0);
    const uint64_t conn = client_w.peer_rpc_connect("127.0.0.1", port);
    ASSERT_NE(conn, 0u);

    // ① NOT_READY 全链：server recv_request → respond_not_ready → client 收到。
    std::thread responder([&] {
        try {
            auto req = server_w.peer_rpc_recv_request(5000);
            if (req.rpc_id_ != 0) {
                server_w.peer_rpc_respond_not_ready(req.conn_id_, req.rpc_id_,
                                                    "warming up");
            }
        } catch (const std::exception&) {
        }
    });
    {
        auto [status, payload] = client_w.peer_rpc_call(conn, "ping", 5000);
        EXPECT_EQ(status, static_cast<uint8_t>(fly::PeerRpcStatus::NOT_READY))
            << "NOT_READY must be delivered as a first-class recoverable status";
        EXPECT_EQ(payload, "warming up");
    }
    responder.join();

    // ② 对端整体 stop_peer_rpc 后 call：发送失败 / 断连 fail → FAILED。
    server_w.stop_peer_rpc();
    {
        auto [status, payload] = client_w.peer_rpc_call(conn, "after-stop", 2000);
        EXPECT_EQ(status, static_cast<uint8_t>(fly::PeerRpcStatus::FAILED));
        EXPECT_FALSE(payload.empty());
    }

    // ③ 无应答小 timeout：FAILED（timeout 语义）。
    const int port2 = server_w.start_peer_rpc_listen("127.0.0.1", 0);
    ASSERT_GT(port2, 0);
    const uint64_t conn2 = client_w.peer_rpc_connect("127.0.0.1", port2);
    ASSERT_NE(conn2, 0u);
    {
        auto [status, payload] = client_w.peer_rpc_call(conn2, "nobody-home", 300);
        EXPECT_EQ(status, static_cast<uint8_t>(fly::PeerRpcStatus::FAILED));
        EXPECT_EQ(payload, "timeout");
    }

    client_w.stop_peer_rpc();
    server_w.stop_peer_rpc();
}

}  // namespace
}  // namespace fly
