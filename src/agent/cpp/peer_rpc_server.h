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
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    // payload 按值交付（server_loop 持有唯一引用——调用方 move 入队，零拷贝）。
    // 返回 optional<payload>：有值则立即回响应（status=0），空则不立即响应。
    using RequestHandler = std::function<std::optional<CMString>(
        uint64_t conn_id, uint64_t rpc_id, uint64_t src_worker_id,
        CMString payload)>;

    // 响应到达回调（客户端角色）：收到 PeerRpcResponseMessage 时调用。
    // status=0 正常响应，status=1 主动失败通知（payload=reason）。
    using ResponseHandler = std::function<void(
        uint64_t conn_id, uint64_t rpc_id, uint8_t status, CMString payload)>;

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
    // (ptr,len) 直发：流式块 payload 免 CMString 中间拷贝。
    bool transport_send_raw(uint64_t conn_id, const char* data, size_t len) {
        return transport_ && transport_->send(conn_id, data, len) > 0;
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
    // 与 read_object 的 L3 模型同构的两段并行：网络线程只做 收包 → 跨帧
    // 块重组 → CRC 验证 → 压缩态记录入有界队列；消费线程 出队 → 解压
    // 直写分段明文缓冲（零 realloc）——网络 IO 与解压真并行（原实现同
    // 线程串行，解压时 wire 只能靠内核缓冲吸收）。
    struct StreamRx {
        uint64_t rpc_id = 0;
        uint8_t direction = 0;   // 0=请求流, 1=响应流
        uint8_t compression_type = 0;
        bool active = false;
        // 跨帧块解析状态（网络线程私有）
        CMString block_acc;      // 当前块记录累积（[16B 头][压缩数据]）
        uint32_t p_unc = 0, p_comp = 0;
        uint64_t p_crc = 0;
        bool have_hdr = false;
        uint64_t consumed = 0;   // 网络线程累计（已喂块流字节）
        // 压缩态记录队列（网络线程产 / 消费线程耗；qx 保护）
        std::mutex qx;
        std::condition_variable q_cv;
        std::deque<CMString> blocks;   // 完整块记录（CRC 已过验）
        size_t q_bytes = 0;
        bool eof = false;        // server_loop：END 已见
        bool failed = false;     // CRC 失配/解压失败/断连——零容忍
        bool done = false;       // 消费线程已退出
        // 消费线程产出（done 后 server_loop 读取，deque 不再变动）
        std::unique_ptr<Compressor> decompressor;  // NONE 时为空（raw 直通）
        struct Seg { CMString buf; size_t fill = 0; };
        std::vector<Seg> segs;   // 分段明文（每段 kRxSegBytes，解压直写）
        uint64_t plain_total = 0;
        uint32_t chunks = 0;
        std::thread consumer;    // join 由 server_loop（END/失败/断连/stop）
    };
    std::unordered_map<uint64_t, CMSharedPtr<StreamRx>> streams_;   // conn_id → 状态（buf_mutex_ 保护）
    // 有界队列上界（压缩态字节）与明文分段大小——对齐 L3 stream_buffer_chunks 量级。
    static constexpr size_t kRxQueueBytes = 64 * 1024 * 1024;
    static constexpr size_t kRxSegBytes = 32 * 1024 * 1024;
    // 流式块流字节喂入：跨帧重组 → CRC 验证 → 压缩态记录入队（有界，
    // 满则阻塞——背压经 TCP 反压发送方）。CRC 失配返回 false——零容忍。
    bool feed_stream_bytes(StreamRx& s, const char* data, size_t n);
    // 消费线程：出队 → 解压/raw 直写 → 分段明文缓冲。eof/failed 且排空
    // 后退出，置 done。不交付、不碰 transport。
    void stream_consume(CMSharedPtr<StreamRx> s);
    // 等消费线程排空退出并 join（END/失败/断连/stop 共用的收尾）。
    // graceful=true 置 eof（排空后正常退出）；false 置 failed（立即判死）。
    void finish_stream_consumer(StreamRx& s, bool graceful);
    // 分段明文拼接为连续 payload（交付拷贝：恰好一次全量）。
    CMString assemble_plain(const StreamRx& s);
    // END 对账：明文总量/块数/消费字节 三重校验。失配返回 false。
    bool verify_stream_end(const StreamRx& s, uint64_t total,
                           uint32_t chunks, uint64_t consumed);

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

// PeerStreamBuffer —— 接收 payload 的 file-protocol 读端（与 FlyBuffer 同
// 语义：read/readline/readinto 供 pickle.load 直用，readinto 直填 pickle
// 工作缓冲——免中间 Python bytes 的全量拷贝）。数据共享持有（零拷贝包装）。
class PeerStreamBuffer {
public:
    PeerStreamBuffer() = default;
    explicit PeerStreamBuffer(CMString data) : data_(std::move(data)) {}

    CMString read(size_t n) {
        const size_t avail = data_.size() - pos_;
        const size_t take = std::min(n, avail);
        CMString out(data_.data() + pos_, take);
        pos_ += take;
        return out;
    }
    CMString readline() {
        const size_t start = pos_;
        const size_t nl = data_.find('\n', start);
        const size_t end = (nl == CMString::npos) ? data_.size() : nl + 1;
        CMString out(data_.data() + start, end - start);
        pos_ = end;
        return out;
    }
    size_t readinto(char* dst, size_t n) {
        const size_t avail = data_.size() - pos_;
        const size_t take = std::min(n, avail);
        std::memcpy(dst, data_.data() + pos_, take);
        pos_ += take;
        return take;
    }
    void seek(size_t p) { pos_ = p; }
    size_t size() const { return data_.size(); }
    size_t pos() const { return pos_; }
    const CMString& data() const { return data_; }

private:
    CMString data_;
    size_t pos_ = 0;
};
using PeerStreamBufferPtr = CMSharedPtr<PeerStreamBuffer>;

// PeerStreamWriter —— worker 间流式大 payload 的写端（file-like，异步压缩）。
//
// 明文经有界队列（kQueueBytes 上界 = 背压）交给独立压缩线程：压缩 → CRC →
// 块记录 → DATA_CHUNK 帧直发（每块一帧，帧头+块头合并小发送，payload
// (ptr,len) 直入连接发送队列，无累积缓冲拷贝）。三个阶段（调用线程序列化 /
// 压缩 / 网络发送）流水重叠——流式的意义即各阶段并行，性能与 payload
// 大小解耦。构造即发 START；finish 关队列、join 压缩线程（排空 + 发尾块 +
// END 对账帧）。压缩算法/级别由调用方指定（config 仅作默认值——接口级
// 压缩指定裁定）。
class PeerStreamWriter {
public:
    static constexpr size_t kFrameBytes = 4 * 1024 * 1024;
    static constexpr size_t kQueueBytes = 4 * 1024 * 1024;   // 明文队列上界（背压）

    PeerStreamWriter(PeerRpcServer* srv, uint64_t conn_id, uint64_t rpc_id,
                     uint8_t direction, CompressionType comp, int level);
    ~PeerStreamWriter();

    void write(const char* data, size_t n);   // 明文入队（满则阻塞；GIL 已在导出层释放）
    // 关闭队列、等待压缩线程排空并发出尾块 + END。返回 false = 流失败。
    bool finish();

    // 统计：finish() 返回后读取（压缩线程结束后才稳定）。
    uint64_t total_uncompressed() const { return total_uncompressed_; }
    uint32_t chunk_count() const { return chunk_count_; }
    bool ok() const { return started_; }       // START 成功（构造即定）
    uint64_t rpc_id() const { return rpc_id_; }
    // 阶段耗时（纳秒，finish 后读）——并行结构验证打点。
    uint64_t write_wait_ns() const { return write_wait_ns_; }  // 生产端等队列空间（下游慢）
    uint64_t compress_ns() const { return compress_ns_; }      // 压缩线程：出队+压缩+组帧
    uint64_t send_ns() const { return send_ns_; }
    // 一次性读取并清零（perf case 分阶段打点用）。
    void take_stage_stats(uint64_t& wait_ns, uint64_t& comp_ns, uint64_t& send_ns) {
        wait_ns = write_wait_ns_.exchange(0);
        comp_ns = compress_ns_.exchange(0);
        send_ns = send_ns_.exchange(0);
    }              // 压缩线程：socket 发送

private:
    void compress_loop(CompressionType comp, int level);
    void enqueue(const char* data, size_t n);
    // 单块记录直发：[25B DATA_CHUNK 帧头][16B 块头] 合并一次小发送，
    // payload (ptr,len) 直发连接发送队列。
    void send_block_frame(const char* payload, size_t n);
    // 空块防御：头齐但无 payload 的记录（当前 Stage 组合不产生）在
    // 流尾补发 header-only 帧；半个块头 = Stage 契约破坏，零容忍断流。
    void flush_pending_block();

    PeerRpcServer* srv_;
    uint64_t conn_id_;
    uint64_t rpc_id_;
    // 明文队列（调用线程生产 / 压缩线程消费）
    std::mutex qm_;
    std::condition_variable q_space_cv_;   // 队列不满（生产等）
    std::condition_variable q_data_cv_;    // 队列非空 / 关闭（压缩线程等）
    std::deque<std::vector<char>> plain_q_;
    size_t q_bytes_ = 0;
    bool producer_closed_ = false;
    bool consumer_stopped_ = false;        // 发送失败：唤醒并丢弃生产
    // 压缩线程与结果
    std::thread thread_;
    bool joined_ = false;
    bool started_ = false;
    bool finished_ = false;
    bool ok_ = false;
    uint64_t total_uncompressed_ = 0;
    uint32_t chunk_count_ = 0;
    // 块头暂存（压缩线程私有）：BlockHeaderStage 分 4 次 emit（4B unc +
    // 4B comp + 8B crc + payload），前 3 次凑齐 16B 块头，第 4 次即 payload
    // ——直发 DATA_CHUNK 帧，不经累积缓冲（原 frame_ 层每流多一次压缩态
    // 全量拷贝；4MB 块下一帧≈一块，分组无收益）。
    char blk_hdr_[16];
    uint32_t blk_hdr_len_ = 0;
    uint64_t frame_off_ = 0;         // 流内偏移（已发压缩字节计数）
    bool send_ok_ = true;            // socket 发送状态（压缩线程私有）
    // 阶段计时（压缩线程累计）
    std::atomic<uint64_t> write_wait_ns_{0};
    std::atomic<uint64_t> compress_ns_{0};
    std::atomic<uint64_t> send_ns_{0};
};

}  // namespace fly
