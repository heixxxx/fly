#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/transport_interface.h>
#include <network/cpp/message_types.h>
#include <network/cpp/data_client_pool.h>
#include <network/cpp/metadata_client.h>
#include <core/cpp/config.h>
#include <agent/cpp/task_executor.h>
#include <agent/cpp/pending_rpc_map.h>
#include <agent/cpp/peer_rpc_server.h>
#include <common/cpp/worker_context.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <filesystem>

namespace fly {

struct PendingTask {
    uint64_t task_id_;
    CMString task_name_;
    CMString task_module_;
    CMVector<CMString> args_;
    CMString write_context_hash_;
    CMVector<VarPayload> var_payloads_;  // Pre-fetched vars from TaskAssignMessage.
};

// WriteRecord — task 执行期间一次写出的对象记录（全名 + 压缩后字节数）。
// 取代原先 current_writes_(vector<name>) + current_write_sizes_(map<name,size>)
// 两个并行容器：单一容器保证 name 与 size 同生命周期，避免分别 clear 导致
// 的 size 丢失（原 end_task 先 clear size map，随后查 size 恒得 0）。
struct WriteRecord {
    CMString full_name_;
    int64_t size_bytes_ = 0;
};;

struct PendingDbPath {
    CMString db_path_;
    CMString data_path_;
    bool completed_ = false;
    bool success_ = false;
};

struct PendingWriteRegister {
    CMString object_name_;
    bool completed_ = false;
    bool success_ = false;
    CMString error_message_;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;
};

struct PendingBackup {
    CMString object_name_;
    CMString db_path_;
    bool completed_ = false;
    bool success_ = false;
};

// Pending state for a synchronous freeze request (awaits master DATABASE_FREEZE_ACK).
struct PendingFreezeAck {
    CMString db_path_;
    bool completed_ = false;
    bool success_ = false;
    TaskErrorType error_type_ = TaskErrorType::UNKNOWN;
};

// PeerRpc 状态码：peer_rpc_call 的返回值 + PendingPeerRpc::status_ 的取值。
// 用 uint8_t 存储（atomic 兼容），枚举值作为语义标签，消除魔法数字。
enum class PeerRpcStatus : uint8_t {
    PENDING   = 0,   // 未完成（wait_for 的 predicate 检查 != PENDING）
    OK        = 1,   // 正常响应
    ERROR     = 2,   // 对端主动 notify_failure / respond_failure（payload 为 reason）
    FAILED    = 3,   // 超时 / 连接断开 / send 失败（payload 为原因描述）
};
// 线上协议 status 见 PeerRpcWireStatus（peer_rpc_server.h）。

// Pending state for a peer RPC（业务 RPC 请求-响应）。rpc_id 匹配请求与响应。
struct PendingPeerRpc {
    std::atomic<uint8_t> status_{static_cast<uint8_t>(PeerRpcStatus::PENDING)};
    CMString payload_;                    // 响应 payload（ok）或失败 reason（error）
    uint64_t conn_id_ = 0;               // 发请求的 P2P 连接（disconnect 时按此匹配 fail）
};

// Pending state for a synchronous var set/get (awaits master VAR_ACK).
struct PendingVarOp {
    CMString var_name_;
    bool completed_ = false;
    bool success_ = false;
    FlyBufferPtr value_;        // get result (zero-copy shared with master response)
    CMString type_name_;
    CMString error_message_;
};

class WorkerAgent {
public:
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port,
                const CMVector<CMString>& attributes = {});
    ~WorkerAgent();
    
    void start();
    void stop();
    bool is_running() const;
    uint64_t get_worker_id() const;
    
    void set_executor(CMSharedPtr<TaskExecutor> executor);

    void begin_task(uint64_t task_id, const CMString& write_context_hash = "");
    void record_write(const CMString& db_path, const CMString& object_name, int64_t size);
    CMVector<WriteRecord> end_task(uint64_t task_id);

    // task 成功时对所有涉及的 db 打 END（提交写入段）。
    void commit_task_segments(const CMVector<WriteRecord>& written_objects);
    // task 失败时本地撤销脏写入（idx ABORT + data truncate + 清内存）。
    void cleanup_failed_task_writes(const CMVector<WriteRecord>& dirty_objects);
    
    bool is_registered() const;
    
    void submit_task(const CMString& name, const CMString& module,
                     const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& required_capabilities = {},
                     float attribute_timeout = -1.0f,
                     const CMString& write_context_hash = "",
                     const CMVector<CMString>& vars = {},
                     int priority = 10);
    
    bool has_pending_task() const;
    bool poll_task();
    bool poll_task_blocking(int timeout_ms = 100);
    
    void register_database(const CMString& db_path, CMSharedPtr<Database> db);
    CMSharedPtr<Database> get_database(const CMString& db_path) const;
    
    std::tuple<bool, bool> request_remote_data(const CMString& object_name);

    bool request_db_path(const CMString& db_path);

    std::pair<CMString, TaskErrorType> register_write_with_master(const CMString& db_path, const CMString& object_name, int64_t compressed_size);
    void request_database_freeze(const CMString& db_path);
    void request_object_remove(const CMString& db_path, const CMString& object_name);
    void request_backup(const CMString& db_path, const CMString& object_name);

    // Var service: synchronous set/get (block on master VAR_ACK) and async remove.
    // These are bound to WorkerAgentContext var funcs at begin_task time, so
    // Database.set_var/get_var/remove_var on a worker reach master over the network.
    bool set_var_sync(const CMString& full_var_name,
                      FlyBufferPtr value, const CMString& type_name);
    std::tuple<bool, FlyBufferPtr, CMString> get_var_sync(const CMString& full_var_name);
    void remove_var_async(const CMString& full_var_name);

    // Message 日志：把配额内的高价值 message 推送到 master（async, no ack）。
    // 由 WorkerAgentContext::push_message（begin_task 绑定）触发，发送 LogMessage。
    void send_message_to_master(LogLevel level, const CMString& domain_id, int32_t source, const CMString& msg);

    // 收到 master 的 MSG_COUNT_REQUEST：把本地 message 触发计数上报（summary 屏障）。
    void on_message_count_request(uint64_t conn_id, const MessageCountRequestMessage& msg);

    // 收到 master 的 MSG_LIMIT_SYNC：整体替换本地配额（不清零计数，支持动态修改）。
    void on_message_limit_sync(uint64_t conn_id, const MessageLimitSyncMessage& msg);

    // Called by the Python executor after _deserialize_args: returns the var
    // payloads inlined by master into the current task's TaskAssignMessage, so
    // they can be injected into the freshly-created Database(s) before the task
    // function runs. Returns and clears the pending vars (one-shot).
    CMVector<VarPayload> take_pending_task_vars();

    void set_worker_property(const CMString& prop);
    void set_worker_property(const CMVector<CMString>& props);
    void remove_worker_property(const CMString& prop);
    void remove_worker_property(const CMVector<CMString>& props);
    CMVector<CMString> get_worker_properties() const;

    // ── 业务 RPC（PeerChannelGroup 底层）──────────────────────────
    // 启动业务端口（服务端）。返回端口（0=失败/已启动）。
    // request_handler 为 nullptr 时表示仅客户端模式（不收请求）。
    int start_peer_rpc_listen(const CMString& host, int port = 0);

    // 客户端：连接到目标 host:port（带 retries 重试）。返回 conn_id（0=失败）。
    uint64_t peer_rpc_connect(const CMString& host, int port,
                               int retries = 2, int retry_interval_ms = 500);

    // 客户端：发送 RPC 请求并等待响应（同步）。返回 {status, payload}。
    // status: 0=ok, 2=error(notify_failure), 3=timeout/disconnect。
    std::pair<uint8_t, CMString> peer_rpc_call(uint64_t conn_id,
                                                const CMString& payload,
                                                int timeout_ms = 30000);

    // 服务端：发送响应（对应收到的 rpc_id，status=0 OK）。
    bool peer_rpc_respond(uint64_t conn_id, uint64_t rpc_id,
                          const CMString& payload);

    // 服务端：对单个请求回失败（status=2 ERROR，精确匹配该 rpc_id 的 pending）。
    // 与 notify_failure（status=1, rpc_id=0 全局通知）的区别：respond_failure 只
    // 让这一个请求的调用方收到失败，不影响同连接上其他 pending 请求。
    bool peer_rpc_respond_failure(uint64_t conn_id, uint64_t rpc_id,
                                   const CMString& reason);

    // 服务端：阻塞等待下一个请求（Python while 循环用）。
    // 返回 {conn_id, rpc_id, src_worker_id, payload}；超时返回 rpc_id=0。
    struct PeerRpcRequest { uint64_t conn_id_; uint64_t rpc_id_; uint64_t src_worker_id_; CMString payload_; };
    PeerRpcRequest peer_rpc_recv_request(int timeout_ms = 30000);

    // 任一方：主动告知对端失败（status=1）。
    bool peer_rpc_notify_failure(uint64_t conn_id, const CMString& reason);

    // 关闭指定连接。
    void peer_rpc_close(uint64_t conn_id);

    // 关闭业务端口（主动退出监听）。
    void stop_peer_rpc();

    int32_t peer_rpc_port() const { return peer_rpc_port_; }

private:
    uint64_t worker_id_;
    CMString master_host_;
    uint16_t master_port_;
    CMVector<CMString> attributes_;
    mutable std::mutex attributes_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    std::atomic<bool> shutdown_triggered_{false};
    
    CMUniquePtr<Reactor> reactor_;
    std::thread reactor_thread_;
    uint64_t master_conn_;
    CMString data_server_host_;
    int32_t data_server_port_ = 0;
    
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_cv_;

    // Bandwidth probe thread (network-aware read priority). Periodically
    // measures RTT/bandwidth to every known data-server peer so TIER2 can
    // prefer better-connected replicas.
    std::thread probe_thread_;
    std::atomic<bool> probe_running_{false};
    std::mutex probe_mutex_;
    std::condition_variable probe_cv_;
    
    CMSharedPtr<TaskExecutor> executor_;

    uint64_t current_task_id_ = 0;
    CMVector<WriteRecord> current_writes_;  // 本 task 的写出记录（全名 + 压缩字节数）
    CMString current_write_hash_;
    
    mutable std::mutex task_queue_mutex_;
    std::condition_variable task_queue_cv_;
    std::queue<PendingTask> task_queue_;
    std::atomic<int> outstanding_tasks_{0};
    
    CMUnorderedMap<CMString, CMSharedPtr<Database>> databases_;

    // Merge 专用 DataWriter 缓存：target_data_path → writer。
    // merge_db 把各源 host 的 data 集中到 master host 的 target_data_path，
    // 按 target_data_path 复用同一个 writer（设计 §5.3：每源 host 一个 writer），
    // 避免 per-object 构造造成文件爆炸。worker 退出时由 ~DataWriter 落盘 idx。
    CMUnorderedMap<CMString, CMUniquePtr<DataWriter>> merge_writers_;
    std::mutex merge_writers_mutex_;

    PendingRpcMap<CMString, PendingDbPath> pending_db_paths_;

    PendingRpcMap<CMString, PendingWriteRegister> pending_write_regs_;

    struct PendingRemove {
        bool completed_ = false;
        bool success_ = false;
    };

    PendingRpcMap<CMString, PendingRemove> pending_removes_;

    // Pending var set/get operations (keyed by var_name, awaiting master VAR_ACK).
    PendingRpcMap<CMString, PendingVarOp> pending_var_ops_;

    // Pending freeze requests (keyed by db_path, awaiting master DATABASE_FREEZE_ACK).
    PendingRpcMap<CMString, PendingFreezeAck> pending_freezes_;

    // 业务 RPC：请求-响应匹配（key=rpc_id）。send_peer_rpc 发请求后 wait_for，
    // on_peer_rpc_response 收到响应后 complete。
    PendingRpcMap<uint64_t, PendingPeerRpc> pending_peer_rpcs_;
    std::atomic<uint64_t> next_rpc_id_{1};

    // 独立业务端口（PeerRpcServer）。worker 间轻量 RPC，与 reactor/DataServer 隔离。
    // start() 时由业务代码（Python PeerChannelGroup）按需启动，stop() 时关闭。
    CMUniquePtr<PeerRpcServer> peer_rpc_server_;
    int32_t peer_rpc_port_ = 0;

    // 服务端收到的请求队列（PeerRpcServer 回调入队，peer_rpc_recv_request 出队）。
    CMVector<PeerRpcRequest> peer_rpc_incoming_;
    // 错误断连的 conn 队列（disconnect_handler 入队，peer_rpc_recv_request 检查后抛异常）。
    CMVector<uint64_t> peer_rpc_error_conns_;
    std::mutex peer_rpc_incoming_mutex_;
    std::condition_variable peer_rpc_incoming_cv_;

    // Vars inlined into the current task's TaskAssignMessage; consumed by the
    // Python executor via take_pending_task_vars() before the task runs.
    CMVector<VarPayload> pending_task_vars_;
    std::mutex pending_task_vars_mutex_;

    void on_register_ack(const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    void on_db_path_response(const DbPathResponseMessage& msg);
    void on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg);
    void on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg);
    void on_remove_ack(uint64_t conn_id, const RemoveAckMessage& msg);
    void on_remove_command(uint64_t conn_id, const RemoveCommandMessage& msg);
    void on_idx_load_command(uint64_t conn_id, const IdxLoadCommandMessage& msg);
    void on_database_freeze_notification(uint64_t conn_id, const DatabaseFreezeNotification& msg);
    void on_database_freeze_ack(uint64_t conn_id, const DatabaseFreezeAckMessage& msg);
    void on_delete_data(uint64_t conn_id, const DeleteDataMessage& msg);
    void on_merge_cleanup(uint64_t conn_id, const MergeCleanupMessage& msg);
    void execute_internal_task(const PendingTask& task);
    // __merge_object：跨机拉源对象压缩字节，落到 merge target_data_path（master host 本地）。
    // 不构造 Database（避免 DataService 全局状态污染），用独立 DataWriter 直接落盘 + 手动 register。
    // 详见 docs/db-merge-design.md §3.4 / §5.1（方案 B）。
    void execute_merge_object(uint64_t task_id, const CMString& short_name,
                              const CMString& source_db_path, const CMString& target_db_path,
                              const CMString& target_data_path);
    // 获取或创建 merge 专用 DataWriter（按 target_data_path 缓存，跨 task 复用，每源 host 一个 writer）。
    DataWriter* get_or_create_merge_writer(const CMString& db_path, const CMString& target_data_path);
    // 注册 PeerRpcServer 的 response_handler + disconnect_handler。
    // start_peer_rpc_listen 和 peer_rpc_connect 共用，避免重复代码。
    void ensure_peer_rpc_handlers();
    void on_disconnect(uint64_t conn_id);

    // Var service handlers.
    void on_var_ack(uint64_t conn_id, const VarAckMessage& msg);
    void on_var_broadcast(uint64_t conn_id, const VarBroadcastMessage& msg);
    
    void heartbeat_loop();
    void bandwidth_probe_loop();
    void touch_master_contact();
    void initiate_shutdown(const CMString& reason);
    void do_cleanup();

    DataClientPool data_client_pool_{Config::instance()->get_int("data_client_pool_size")};
    MetadataClient metadata_client_;

    // Master liveness tracking — seconds since epoch (atomic for cross-thread access)
    std::atomic<int64_t> last_master_contact_{0};
    static constexpr int MASTER_TIMEOUT_SECONDS = 120;
};

}  // namespace fly