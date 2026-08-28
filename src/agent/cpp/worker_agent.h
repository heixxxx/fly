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
#include <common/cpp/concurrent_map.h>
#include <monitor/cpp/monitor_sampler.h>
#include <monitor/cpp/task_resource_tracker.h>
#include <task/cpp/worker_manager.h>   // WorkerRole（worker role 静态身份枚举）
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <queue>
#include <filesystem>

namespace fly {

// worker 退出性质（用户裁定：master/worker 双侧显式区分正常退出与异常退出，
// 不靠 reason 字符串猜测）。枚举值仅内部与 WorkerExitMessage 诊断字段使用，
// 外部观测方看进程退出码（exit_code：graceful=0 / abnormal=3）。
enum class ExitReason : uint8_t {
    MASTER_SHUTDOWN = 0,       // master ShutdownMessage 优雅关停 → graceful
    LOCAL_STOP = 1,            // stop() API 本地显式停止 → graceful
    MASTER_LOST = 2,           // 心跳超时/连接丢失/重连宽限耗尽 → abnormal
    REGISTRATION_REJECTED = 3, // 重复 worker id 被 master 拒绝 → abnormal
};

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

// 转发提交的同步等待（Ack 强语义）：master 入图确认带回 task_id。
struct PendingTaskSubmit {
    bool completed_ = false;
    bool accepted_ = false;
    uint64_t task_id_ = 0;
};

class WorkerAgent {
public:
    // role：静态身份（"hybrid" 默认 / "storage_only"），注册时上报、不可变更；
    // 独立于 attributes（可变、参与调度匹配）。非法值 WARN 回退 hybrid。
    WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port,
                const CMVector<CMString>& attributes = {}, const CMString& role = "hybrid");
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
    
    // task 体内提交转发（Ack 强语义，用户确认语义）：同步 RPC——master 入图
    // 确认（TaskSubmitAck 带回 task_id）才返回；断连窗口按 A 类挂起（入统一
    // 重放队列阻塞等注册确认后重放拿 Ack）。返回 task_id（0=失败/超时）。
    uint64_t submit_task(const CMString& name, const CMString& module,
                     const CMVector<CMString>& args,
                     const CMVector<CMString>& inputs,
                     const CMVector<CMString>& required_capabilities = {},
                     float attribute_timeout = -1.0f,
                     const CMString& write_context_hash = "",
                     const CMVector<CMString>& vars = {},
                     int priority = 10,
                     const CMString& owner_db_path = "");
    
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
    // 静态身份（构造时由 role 字符串解析，注册上报，不可变更）：
    // 0=hybrid，1=storage_only（WorkerRole）。
    uint8_t role_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> registered_{false};
    // ── 断连/未注册窗口的统一重放队列（用户确认语义）──────────────────
    // 注册完成前不处理发往 master 的消息：
    //   A 类（调用方依赖 master 裁决：DbPathRequest/Freeze/VarSet/VarGet/
    //        RemoveRequest/WriteRegister）——登记各自 PendingRpcMap 等待者
    //        后入队，调用方阻塞在同步点；注册确认后按 FIFO 重放、各自 Ack
    //        唤醒；worker 终止时批量 fail。
    //   B 类（通知/后台性质：BackupRequest/能力更新/ObjectRemoved/TaskSubmit/
    //        var 异步删除）——入队即返回；注册确认后重放。
    // FIFO 保序：同一 task 的消息（DbPathRequest → WriteRegister → …）按
    // 入队顺序重放；Task 上报经 flush_pending_reports 固定最后。
    struct PendingMasterSend {
        std::function<void(uint64_t conn)> replay_;  // 注册后执行（conn 参数
                                                     // 取当时的 master_conn_）
    };
    std::mutex pending_master_sends_mutex_;
    CMVector<PendingMasterSend> pending_master_sends_;
    std::atomic<bool> shutdown_triggered_{false};
    // 退出性质（initiate_shutdown 首次进入时写入；详见 exit_reason_graceful）。
    std::atomic<ExitReason> exit_reason_{ExitReason::LOCAL_STOP};
    
    CMUniquePtr<Reactor> reactor_;
    std::thread reactor_thread_;
    // master 连接（atomic：断连后重连线程更新，各 handler 线程读——隐式转换
    // 运算符使既有读写点无需改动）。0=未连接。
    std::atomic<uint64_t> master_conn_{0};
    CMString data_server_host_;
    int32_t data_server_port_ = 0;

    // ── 断连重连（网络闪断，宽限窗口内指数退避；master 挂=全群失败，超时退出）──
    std::atomic<bool> reconnecting_{false};
    // {registered_, reconnecting_} 状态迁移互斥锁（P3-27）：on_register_ack 与
    // on_disconnect 的 check-act 必须原子，消除「断连置位后、重连线程 spawn 前
    // 被残留 ack 清除重连标志」的交错窗口（重连线程入口即静默退出）。
    std::mutex register_state_mutex_;
    // start() 尾段与 initiate_shutdown 的生命周期标志写互斥（秒拒竞争根治）：
    // dup ack 可能在 start 尾段到达，两处的 {shutdown_triggered_, running_,
    // 三线程 flag} 写必须串行——否则 start 覆盖 initiate 的关闭标志
    //（is_running 恒真 + 幂等闸门锁死 join，实测 300s 卡死），或检查点与
    // 覆盖点之间留 TOCTOU 残窗。锁序：本锁 → register_ack_mutex_/
    // heartbeat_mutex_/probe_mutex_（无反向获取）。
    std::mutex shutdown_state_mutex_;
    std::thread reconnect_thread_;
    // 断连期间完成的 task 上报缓冲：重连注册确认后按序 flush（fire-and-forget
    // 的其它消息不缓冲——心跳/注册自然恢复，task 结果不可丢）。
    struct PendingReport {
        bool is_complete_;
        TaskCompleteMessage complete_;
        TaskFailedMessage failed_;
    };
    std::mutex pending_reports_mutex_;
    CMVector<PendingReport> pending_reports_;
    
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_cv_;

    // ---- monitor 采样上报线程（与心跳完全解耦；原心跳 RSS 采样迁移至此）----
    // 采样模型（用户裁定原则）：task start/end、IO 读写、assign/断连等事件
    // 发生时刻是天然采样点，固定间隔只是兜底节奏——事件密集期样本自然加密、
    // 空闲期稀疏。每 monitor_sample_interval_ms 周期采样；task 执行窗口内
    // 加密至 monitor_exec_sample_interval_ms；事件点经 sample_now_event 节流
    // 后入缓冲。全部样本共用最小间距节流（事件风暴不刷爆 DB）。每
    // monitor_report_interval_ms 成组 MONITOR_SAMPLE 上报；发送失败/断连窗口
    // 样本缓冲不丢，下次成组补发（沿用原心跳 pending_rss_* 语义）。
    std::thread monitor_thread_;
    std::atomic<bool> monitor_running_{false};
    std::mutex monitor_mutex_;
    std::condition_variable monitor_cv_;
    MonitorSampler monitor_sampler_;  // 周期 + 事件采样共用（内部互斥，多线程安全）
    // 采样缓冲 + 节流（monitor 线程与 reactor lane/执行线程的事件采样并发
    // 访问，互斥保护）。
    std::mutex samples_mutex_;
    CMVector<MonitorSample> pending_samples_;
    uint64_t last_sample_epoch_ms_ = 0;  // 节流基准（受 samples_mutex_ 保护）
    int64_t sample_gap_ms_ = 200;        // 最小采样间距（start 时读 config）
    void monitor_report_loop();
    // 事件驱动采样：任意线程可调（assign/执行起止/internal/断连/注册完成等
    // cluster 事件时刻的 worker 全维度快照，kind=1）。
    void sample_now_event();
    // 节流后入缓冲（周期与事件共用；间距不足返回 false）。
    bool append_sample_throttled(MonitorSample sp);

    // 对象级 IO 明细上报（尽力而为通道；poll_task 执行后调用）。
    void report_task_io(const TaskExecResult& result);

    // task 执行窗口资源归属：poll_task（begin/end）+ monitor 采样线程与
    // IO 事件峰值（add_sample）+ send_master_or_buffer（take_agg 填消息字段）三方协作。
    TaskResourceTracker task_resource_tracker_;

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
    // databases_ 容器锁：reactor(lane) handler 与 Python 执行线程并发访问。
    // databases_ 容器锁：reactor(lane) handler 与 Python 执行线程并发访问。
    mutable std::shared_mutex databases_mutex_;

    // Merge 专用 DataWriter 缓存：target_data_path → writer。
    // merge_db 把各源 host 的 data 集中到 master host 的 target_data_path，
    // 按 target_data_path 复用同一个 writer（设计 §5.3：每源 host 一个 writer），
    // 避免 per-object 构造造成文件爆炸。worker 退出时由 ~DataWriter 落盘 idx。
    // get_or_insert 语义：factory（含 writer_id 生成 + DataWriter 构造）锁内执行。
    ConcurrentUnorderedMap<CMString, CMSharedPtr<DataWriter>> merge_writers_;

    PendingRpcMap<CMString, PendingDbPath> pending_db_paths_;

    PendingRpcMap<CMString, PendingWriteRegister> pending_write_regs_;

    struct PendingRemove {
        bool completed_ = false;
        bool success_ = false;
    };

    PendingRpcMap<CMString, PendingRemove> pending_removes_;
    // 转发提交的 Ack 等待（key=worker 侧生成的 request_id）。
    PendingRpcMap<uint64_t, PendingTaskSubmit> pending_task_submits_;
    std::atomic<uint64_t> next_submit_request_id_{1};

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

    void on_register_ack(uint64_t conn_id, const RegisterAckMessage& msg);
    void on_task_assign(const TaskAssignMessage& msg);
    void on_shutdown(const ShutdownMessage& msg);
    // STOP_NOW（master 快速退出通道）：进程级自杀（kill SIGKILL）。testonly
    // 编译下可被 hook 拦截（库对象在单测进程内不能真杀测试进程）。
    void on_stop_now(const StopNowMessage& msg);
    void on_db_path_response(const DbPathResponseMessage& msg);
    void on_write_register_ack(uint64_t conn_id, const WriteRegisterAckMessage& msg);
    void on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg);
    void on_remove_ack(uint64_t conn_id, const RemoveAckMessage& msg);
    void on_remove_command(uint64_t conn_id, const RemoveCommandMessage& msg);
    void on_idx_load_command(uint64_t conn_id, const IdxLoadCommandMessage& msg);
    // master 下行属性追加（ensure_workers）：去重并入自身 attributes_，经既有
    // WORKER_PROPERTY_UPDATE 上行回报（视图更新复用现有链路）。
    void on_worker_property_assign(uint64_t conn_id, const WorkerPropertyAssignMessage& msg);
    // 自动补齐：在本 host 唤起 storage_only worker（posix_spawn /proc/self/exe
    // + SETSID 脱离进程树 + detached waitpid 回收；Config 落盘传递）。
    void on_storage_spawn_request(uint64_t conn_id, const StorageSpawnRequestMessage& msg);
    void on_database_freeze_notification(uint64_t conn_id, const DatabaseFreezeNotification& msg);
    void on_database_freeze_ack(uint64_t conn_id, const DatabaseFreezeAckMessage& msg);
    void on_delete_data(uint64_t conn_id, const DeleteDataMessage& msg);
    void on_merge_cleanup(uint64_t conn_id, const MergeCleanupMessage& msg);
    // 失败清理：删除本 worker merge writer 的产物文件（见 on_merge_cleanup purge 分支）。
    void purge_merge_products(const MergeCleanupMessage& msg);
    void execute_internal_task(const PendingTask& task);
    // __merge_object：跨机拉源对象压缩字节，落到 merge target_data_path（master host 本地）。
    // 不构造 Database（避免 DataService 全局状态污染），用独立 DataWriter 直接落盘 + 手动 register。
    // 详见 docs/db-merge-design.md §3.4 / §5.1（方案 B）。
    void execute_merge_object(uint64_t task_id, const CMString& short_name,
                              const CMString& source_db_path, const CMString& target_db_path,
                              const CMString& target_data_path);
    // 获取或创建 merge 专用 DataWriter（按 target_data_path 缓存，跨 task 复用，每源 host 一个 writer）。
    CMSharedPtr<DataWriter> get_or_create_merge_writer(const CMString& db_path, const CMString& target_data_path);
    // 注册 PeerRpcServer 的 response_handler + disconnect_handler。
    // start_peer_rpc_listen 和 peer_rpc_connect 共用，避免重复代码。
    void ensure_peer_rpc_handlers();
    void on_disconnect(uint64_t conn_id);

    // Var service handlers.
    void on_var_ack(uint64_t conn_id, const VarAckMessage& msg);
    void on_var_broadcast(uint64_t conn_id, const VarBroadcastMessage& msg);
    
    void heartbeat_loop();
    // 首注册/重发共用的 REGISTER 构造与发送（幂等：master 对同 conn 重发走
    // 正常注册路径）。
    void send_register_message();
    // 注册守望线程：等待 RegisterAck 的事件驱动重发兜底（P3-23）。仅覆盖
    // 「master 活着但注册/ack 被应用层吞掉」的场景——连接级丢失由
    // on_disconnect → reconnect_loop 的事件驱动路径恢复（毫秒级，无需超时）。
    // cv 等 ack（注册成功即刻退出，零空转）；超时则指数退避重发
    // （initial ×2 上限 30s）；reconnecting_ 期间让位给 reconnect_loop。
    void register_watchdog_loop();
    std::thread register_watchdog_thread_;
    std::atomic<bool> register_watchdog_running_{false};
    std::mutex register_ack_mutex_;
    std::condition_variable register_ack_cv_;
    // 断连重连线程：指数退避 reactor_->connect（initial ×2 上限 10s），总窗口
    // worker_reconnect_timeout；成功 → 重发 Register（原 worker_id/data 端口）
    // → RegisterAck 后 flush 缓冲；超时 → initiate_shutdown（干净退出）。
    void reconnect_loop();
    // task 上报出口：连接有效直发；重连中缓冲（重连后 flush）。
    void send_master_or_buffer(const TaskCompleteMessage& msg);
    void send_master_or_buffer(const TaskFailedMessage& msg);
    void flush_pending_reports();
    // 统一重放队列：入队（未注册窗口调用；replay 闭包在注册确认后以当前
    // master_conn_ 执行）与重放（on_register_ack，先于 task 上报 flush）。
    void enqueue_master_send(std::function<void(uint64_t conn)> replay);
    void replay_pending_master_sends();
    void bandwidth_probe_loop();
    // connect master 指数退避重试（见 worker_agent.cpp 实现处注释：窗口与 master
    // 占位符共用 worker_register_timeout，两侧统一 5min 保活）。
    uint64_t connect_master_with_retry(class ConnectionManager& transport);
    void touch_master_contact();
    // 退出统一入口（幂等）。reason 显式区分退出性质（用户裁定语义）：
    // graceful 分支 INFO + 通知 master（WORKER_EXIT，关连接前）+ 进程退出码 0；
    // abnormal 分支 ERR + 进程退出码 3。detail 为诊断细节（随日志与
    // WorkerExitMessage 带出）。
    void initiate_shutdown(ExitReason reason, const CMString& detail);
    void do_cleanup();

    DataClientPool data_client_pool_{Config::instance()->get_int("data_client_pool_size")};
    MetadataClient metadata_client_;

    // Master liveness tracking — seconds since epoch (atomic for cross-thread access)
    std::atomic<int64_t> last_master_contact_{0};
    static constexpr int MASTER_TIMEOUT_SECONDS = 120;

public:
    // 退出性质访问（用户裁定：正常/异常显式分支，不靠字符串猜测）。首个触发
    // 原因最准确——shutdown_triggered_ exchange 防重入，仅首次进入时写入。
    static bool exit_reason_graceful(ExitReason r) {
        return r == ExitReason::MASTER_SHUTDOWN || r == ExitReason::LOCAL_STOP;
    }
    ExitReason exit_reason() const { return exit_reason_.load(); }
    // 进程退出码（OS 层，bsub/ssh/运维脚本的外部观测口）：graceful=0、
    // abnormal=3（避开既有占用 0/1/42/77/78——正常/脚本错误/Python abort/崩溃取证）。
    int exit_code() const {
        return exit_reason_graceful(exit_reason_.load()) ? 0 : 3;
    }

#ifdef FLY_ENABLE_TEST_HOOKS
public:
    // 仅测试用：直接驱动退出入口（ExitReason 语义/退出码断言，无需网络路径）。
    void initiate_shutdown_for_testing(ExitReason reason, const CMString& detail) {
        initiate_shutdown(reason, detail);
    }
    // 仅测试用（release 编译零开销）：connect_master_with_retry 每次 connect 尝试的
    // 时间戳（验证指数退避递增）。
    CMVector<std::chrono::steady_clock::time_point> connect_attempts_for_testing_;
    // 仅测试用：execute_merge_object 对命中 short_name 的对象模拟写盘失败
    //（确定性验证 merge task 走 TaskFailed 而非假成功）。
    CMUnorderedSet<CMString> merge_write_fail_for_testing_;
    // 仅测试用：断连期间缓冲的 task 上报数量（缓冲/flush 测试用）。
    size_t pending_report_count_for_testing();
    // 仅测试用：reconnect_loop 入口触发（P3-27 回归——park 重连线程，确定性
    // 构造「断连后残留 ack 清除重连标志致重连线程静默退出」的窗口）。
    std::function<void()> reconnect_entry_hook_for_testing_;
    // 仅测试用：STOP_NOW 拦截（默认空=真自杀路径；安装后记录 reason 不杀进程，
    // 供库对象单测观察快速退出语义）。
    std::function<void(const CMString& reason)> stop_now_hook_for_testing_;
    // 仅测试用：直接驱动 on_register_ack（构造「断连后残留 ack 到达」场景，
    // conn_id 由测试指定）。
    void on_register_ack_for_testing(uint64_t conn_id, const RegisterAckMessage& msg) {
        on_register_ack(conn_id, msg);
    }
    // 仅测试用：start() 在 send_register_message 之后、线程 spawn 之前触发
    // （P3-28 回归——注入「启动中途收到 duplicate 拒绝」的确定性场景）。
    std::function<void()> post_register_send_hook_for_testing_;
    // 仅测试用：模拟 master 连接闪断——真实关闭 TCP（master 侧 epoll 收
    // FIN/EOF 后清连接表，与真实闪断一致）再触发本侧 on_disconnect 重连
    // 路径。只回调不关 fd 的旧模拟会让 master 连接表残留旧 conn，撞上
    // 先到先得的重复注册判定（DisconnectReconnectsAndReports 实测）。
    void simulate_master_disconnect_for_testing() {
        uint64_t conn = master_conn_.load();
        if (conn != 0) {
            reactor_->close_connection(conn);
        }
        on_disconnect(conn);
    }
    // 未注册窗口写登记的回归测试用（语义：pending 阻塞 + 终止唤醒，不放行）。
    std::pair<CMString, TaskErrorType> register_write_with_master_for_testing(
        const CMString& db_path, const CMString& object_name, int64_t size) {
        return register_write_with_master(db_path, object_name, size);
    }
    // 模拟 worker 终止对 pending 写注册的批量失败唤醒（initiate_shutdown
    // 同款逻辑，供无网络单测驱动）。
    void fail_pending_write_regs_for_testing() {
        pending_write_regs_.complete_all_if(
            [](const PendingWriteRegister&) { return true; },
            [](PendingWriteRegister& p) {
                p.completed_ = true;
                p.success_ = false;
                p.error_type_ = TaskErrorType::WRITE_REGISTRATION_FAILED;
                p.error_message_ = "worker shutting down before registration confirmed";
            });
    }
    // 模拟 worker 终止对其余 A 类同步 RPC（freeze）的批量失败唤醒。
    void fail_pending_freezes_for_testing() {
        pending_freezes_.complete_all_if(
            [](const PendingFreezeAck&) { return true; },
            [](PendingFreezeAck& p) {
                p.completed_ = true;
                p.success_ = false;
                p.error_type_ = TaskErrorType::WRITE_REGISTRATION_TIMEOUT;
            });
    }
#endif
};

}  // namespace fly