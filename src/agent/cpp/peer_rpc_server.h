#pragma once

#include <network/cpp/connection_manager.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <common/cpp/common_types.h>
#include <log/cpp/logger.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fly {

// 线上协议 status（PeerRpcResponseMessage.status_ 字段的取值）。
// 与 WorkerAgent 内部 PeerRpcStatus 分开：BYE 是连接管理信号，在
// response_handler 层拦截处理，不传到 pending RPC / 调用方。
enum class PeerRpcWireStatus : uint8_t {
    OK              = 0,  // 正常响应
    NOTIFY_FAILURE  = 1,  // 主动失败通知（rpc_id=0，payload=reason）
    RESPOND_FAILURE = 2,  // 对单个请求回失败（精确匹配 rpc_id）
    BYE             = 3,  // 主动断连信号（优雅关闭握手）
};

// PeerRpcServer — 独立业务端口，提供 worker 间的轻量 RPC（请求-响应）通信。
//
// 与 reactor（控制面）和 DataServer（数据面）隔离：
//   - 独立 ConnectionManager 实例 + 独立线程（不阻塞 reactor/DataServer）
//   - 独立端口（OS 动态分配），生命周期由 WorkerAgent 管理
//   - stop() 彻底关闭端口 + 清理所有连接（主动退出监听接口）
//
// 消息走 MessageProtocol 编码（与 reactor 同帧格式），但 payload 是裸 bytes
// （业务自定义序列化，不经 bitsery/pickle 的 DB 包装）。
//
// 两种角色：
//   - 服务端（check worker）：listen + accept + on_request 回调处理请求
//   - 客户端（compute worker）：connect_peer + send_rpc（配合 PendingRpcMap 等响应）
class PeerRpcServer {
public:
    // 请求到达回调（服务端角色）：收到 PeerRpcRequestMessage 时调用。
    // 返回 optional<payload>：有值则立即回响应（status=0），空则不立即响应。
    using RequestHandler = std::function<std::optional<CMString>(
        uint64_t conn_id, uint64_t rpc_id, uint64_t src_worker_id,
        const CMString& payload)>;

    // 响应到达回调（客户端角色）：收到 PeerRpcResponseMessage 时调用。
    // status=0 正常响应，status=1 主动失败通知（payload=reason）。
    using ResponseHandler = std::function<void(
        uint64_t conn_id, uint64_t rpc_id, uint8_t status,
        const CMString& payload)>;

    // 连接断开回调（客户端角色）：P2P 连接断开时调用（对端关闭/网络断）。
    // WorkerAgent 用它 fail 该 conn_id 上所有 pending RPC，避免 rpc_call 死等。
    using DisconnectHandler = std::function<void(uint64_t conn_id)>;

    PeerRpcServer();
    ~PeerRpcServer();

    PeerRpcServer(const PeerRpcServer&) = delete;
    PeerRpcServer& operator=(const PeerRpcServer&) = delete;

    // 启动服务端：绑定独立业务端口（port=0 让 OS 分配）。返回实际端口，0=失败。
    // request_handler 是请求到达时的回调（服务端角色）。
    int listen(const CMString& host, int port, RequestHandler request_handler);

    // 设置响应到达回调（客户端角色）。收到 PEER_RPC_RESPONSE 时调用。
    void set_response_handler(ResponseHandler handler);

    // 设置连接断开回调（客户端角色）。P2P 连接断开时调用。
    void set_disconnect_handler(DisconnectHandler handler);

    // 客户端：连接到目标 host:port，返回 conn_id（0=失败）。
    // 带 retries 次重试（覆盖对端未就绪）。
    uint64_t connect_peer(const CMString& host, int port,
                          int retries = 2, int retry_interval_ms = 500);

    // 客户端：发送 RPC 请求。响应由 PendingRpcMap（调用方管理）匹配 rpc_id。
    // 此方法只负责发出请求帧，不等响应。
    bool send_request(uint64_t conn_id, uint64_t rpc_id,
                      uint64_t src_worker_id, const CMString& payload);

    // 服务端：发送响应（对应之前收到的 rpc_id）。
    bool send_response(uint64_t conn_id, uint64_t rpc_id,
                       uint8_t status, const CMString& payload);

    // 主动告知对端失败（status=1，payload=reason）。任一方可调。
    bool notify_failure(uint64_t conn_id, const CMString& reason);

    // 关闭指定连接（直接 TCP close，不发 BYE）。
    void close_connection(uint64_t conn_id);

    // 主动断连：发 BYE → 同步等服务端回 BYE_ACK（5s 超时）→ close。
    // 客户端侧优雅关闭握手。BYE 丢失或超时时直接 close（DISCONNECT 兜底）。
    bool send_bye(uint64_t conn_id);

    bool is_connected(uint64_t conn_id) const;

    // 彻底关闭：停止监听 + 关闭所有连接 + 线程退出（主动退出监听接口）。
    void stop();

    bool is_running() const { return running_.load(); }

private:
    void server_loop();
    // 服务端收到 BYE 的处理：回 BYE_ACK + close + 标记。
    void handle_bye(uint64_t conn_id);

    CMUniquePtr<ConnectionManager> transport_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    RequestHandler request_handler_;
    ResponseHandler response_handler_;
    DisconnectHandler disconnect_handler_;

    // 每连接的接收缓冲（累积半截帧，MessageProtocol::decode 原地切帧）
    std::mutex buf_mutex_;
    std::unordered_map<uint64_t, CMString> recv_bufs_;

    // BYE 握手状态：
    //   bye_closed_conns_：已通过 BYE 正常关闭的 conn（DISCONNECT 时静默，不触发 disconnect_handler）
    //   bye_ack_conns_：   客户端侧收到的 BYE_ACK（send_bye 的 wait 条件）
    //   bye_pending_conns_：客户端已发 BYE 待 ACK 的 conn（区分服务端收到的 BYE vs 客户端收到的 BYE_ACK）
    std::mutex bye_mutex_;
    std::condition_variable bye_cv_;
    std::unordered_set<uint64_t> bye_closed_conns_;
    std::unordered_set<uint64_t> bye_ack_conns_;
    std::unordered_set<uint64_t> bye_pending_conns_;
};

}  // namespace fly
