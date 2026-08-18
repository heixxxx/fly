// PeerRpcServer 单元测试（2026-08-16 补覆盖：v2 daemon 通信底座此前零单测，
// 仅 qa/solver 的 v2 case 兜底）。真实 localhost TCP 验证：
//   listen 端口分配 / 端到端 RPC 往返 / 异步响应 / 失败通知 / BYE 优雅关闭 /
//   connect 重试 / stop 清理。

#include <gtest/gtest.h>
#include <agent/cpp/peer_rpc_server.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

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

class PeerRpcServerTest : public ::testing::Test {
protected:
    void TearDown() override {
        if (server) server->stop();
        if (client) client->stop();
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
