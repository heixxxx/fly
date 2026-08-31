#pragma once

#include <network/cpp/connection_manager.h>
#include <network/cpp/message_protocol.h>
#include <network/cpp/message_types.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/pipeline.h>
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
    NOT_READY       = 4,  // 参数未就绪（可恢复——调用方跳过重试；payload=诊断消息）
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

    // ── 流式大 payload（流插件化 2026-08-31）──
    // 发送：send_stream_start → send_stream_data × N（4MB 切帧由
    // PeerStreamWriter 封装）→ send_stream_end。块级 CRC/END 对账承担
    // 完整性；连接独占（START 至 END 之间无其他帧）。
    bool send_stream_start(uint64_t conn_id, uint64_t rpc_id, uint8_t direction,
                           uint8_t compression_type);
    bool send_stream_data(uint64_t conn_id, const char* data, size_t n);
    bool send_stream_end(uint64_t conn_id, uint64_t rpc_id,
                         uint64_t total_uncompressed, uint32_t chunk_count,
                         uint64_t consumed);
    // 便捷封装：压缩块流经管线（压缩+块格式化）→ 4MB 切帧 → END。
    // 返回统计（total_uncompressed/chunk_count），失败返回 false。
    bool send_stream_payload(uint64_t conn_id, uint64_t rpc_id, uint8_t direction,
                             const CMString& payload, CompressionType comp,
                             int level, uint64_t& total_out, uint32_t& chunks_out);

    // 流式帧发送（PeerStreamWriter 用）：转 ConnectionManager::send
    // （内部完整发送语义：部分发送/EAGAIN 由写缓冲 + epoll 驱动排空）。
    bool transport_send_raw(uint64_t conn_id, const CMString& data) {
        return transport_ && transport_->send(conn_id, data) > 0;
    }

    // 主动告知对端失败（status=1，payload=reason）。任一方可调。
    bool notify_failure(uint64_t conn_id, const CMString& reason);

    // 对单个请求回"未就绪"（status=NOT_READY，可恢复——调用方跳过重试，
    // 不判死）。与 RESPOND_FAILURE（真失败）在协议层区分。
    bool send_not_ready(uint64_t conn_id, uint64_t rpc_id,
                        const CMString& reason);

    // 关闭指定连接（直接 TCP close，不发 BYE）。
    void close_connection(uint64_t conn_id);

    // 主动断连：发 BYE → 同步等服务端回 BYE_ACK（5s 超时）→ close。
    // 客户端侧优雅关闭握手。BYE 丢失或超时时直接 close（DISCONNECT 兜底）。
    bool send_bye(uint64_t conn_id);

    bool is_connected(uint64_t conn_id) const;

    // 彻底关闭：停止监听 + 关闭所有连接 + 线程退出（主动退出监听接口）。
    void stop();

    bool is_running() const { return running_.load(); }

#ifdef FLY_ENABLE_TEST_HOOKS
public:
    // ── 测试专用接口：仅 test_hooks 库变体激活（release 产物不含）──
    // send_bye 的 cv 唤醒后、本端 bye_closed 标记前触发（参数：conn_id,
    // got_ack；不持 bye_mutex_——持锁会阻塞 server_loop 的 DISCONNECT 处理，
    // 反而掩盖竞态）。测试用它 park 调用方，确定性构造「服务端 ACK+close 后
    // DISCONNECT 事件抢在调用方标记前被 server_loop 处理」的竞态窗口
    // （P3-25 回归用）。
    std::function<void(uint64_t conn_id, bool got_ack)> bye_wake_hook_for_testing_;
#endif

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

    // 流式接收状态（连接独占：START 至 END 之间该连接只有流数据帧）。
    // compressed 累积压缩块流字节（对端 TCP 反压 + 本端组装口径——内存
    // 峰值 = payload 压缩态，见性能分析文档 §3.2 裁定）。
    struct StreamRx {
        uint64_t rpc_id = 0;
        uint8_t direction = 0;   // 0=请求流, 1=响应流
        uint8_t compression_type = 0;
        CMString compressed;     // 压缩块流字节
        uint64_t consumed = 0;
        bool active = false;
    };
    std::unordered_map<uint64_t, StreamRx> streams_;   // conn_id → 状态（buf_mutex_ 保护）
    // 流完成：对账 + 解压 → 明文 payload。失败（对账失配/解压错误）返回 false。
    bool finish_stream(uint64_t conn_id, StreamRx& s, CMString& payload_out);

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

// PeerStreamWriter —— worker 间流式大 payload 的写端（file-like）。
//
// pickle.dump(obj, writer) 直入：明文经压缩管线（WritePipeline：压缩 →
// CRC → 块记录）逐块产出，4MB 切 DATA_CHUNK 帧独占连接发送。构造即发
// START，finish 发尾块 + END 对账帧。压缩算法/级别由调用方指定（config
// 仅作默认值——接口级压缩指定裁定）。
class PeerStreamWriter {
public:
    static constexpr size_t kFrameBytes = 4 * 1024 * 1024;

    PeerStreamWriter(PeerRpcServer* srv, uint64_t conn_id, uint64_t rpc_id,
                     uint8_t direction, CompressionType comp, int level);

    void write(const char* data, size_t n);
    // 尾块 + END 对账帧。返回 false = 发送失败（START 后首次失败亦然）。
    bool finish();

    uint64_t total_uncompressed() const { return pipeline_ ? pipeline_->total_uncompressed() : 0; }
    uint32_t chunk_count() const { return pipeline_ ? pipeline_->chunk_count() : 0; }
    bool ok() const { return ok_; }

private:
    void on_pipeline_bytes(const char* d, size_t n);
    void flush_frame();

    PeerRpcServer* srv_;
    uint64_t conn_id_;
    uint64_t rpc_id_;
    bool started_ = false;
    bool finished_ = false;
    bool ok_ = false;
    CMString frame_;
    uint64_t frame_off_ = 0;
    std::unique_ptr<fly::WritePipeline> pipeline_;
};

}  // namespace fly
