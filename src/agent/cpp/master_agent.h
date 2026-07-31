#pragma once

#include <network/cpp/reactor.h>
#include <network/cpp/connection_manager.h>
#include <network/cpp/message_types.h>
#include <network/cpp/data_client_pool.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_service.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <task/cpp/task_scheduler.h>
#include <task/cpp/task_manager.h>
#include <task/cpp/heartbeat_monitor.h>
#include <log/cpp/logger.h>
#include <message/cpp/message_sink.h>
#include <core/cpp/config.h>
#include <common/cpp/common_types.h>
#include <common/cpp/worker_context.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <signal.h>
#include <memory>

namespace fly {

struct FailedTaskRecord {
    uint64_t task_id_ = 0;
    CMString name_;
    CMString module_;
    CMVector<CMString> args_;
    CMVector<CMString> inputs_;
    CMVector<CMString> outputs_;
    CMVector<CMString> required_capabilities_;
    CMString error_message_;

    FLY_SERIALIZE(task_id_, name_, module_, args_, inputs_, outputs_,
                  required_capabilities_, error_message_);
};

struct FailedTaskFile {
    CMVector<FailedTaskRecord> records_;
    FLY_SERIALIZE(records_);
};

class MasterAgent {
public:
    MasterAgent(const CMString& host, uint16_t port);
    ~MasterAgent();

    void start();
    void stop();
    bool is_running() const;

    CMVector<uint64_t> get_connected_workers() const;
    CMVector<std::pair<uint64_t, CMString>> get_worker_hostnames() const;
    void add_worker_hostname(uint64_t worker_id, const CMString& hostname);
    size_t get_connection_count() const;

    void submit_task(uint64_t task_id, const CMString& name,
                    const CMString& module, const CMVector<CMString>& args,
                    const CMVector<CMString>& inputs = {},
                    const CMVector<CMString>& outputs = {},
                    const CMVector<CMString>& required_capabilities = {},
                    float attribute_timeout = -1.0f,
                    const CMString& write_context_hash = "",
                    const CMVector<CMString>& vars = {},
                    int priority = 10);

    CMVector<uint64_t> get_pending_tasks() const;
    CMVector<uint64_t> get_running_tasks() const;
    CMVector<uint64_t> get_completed_tasks() const;
    CMVector<uint64_t> get_failed_tasks() const;
    CMString get_task_error(uint64_t task_id) const;

    CMVector<uint64_t> get_idle_workers() const;

    void restart_failed_tasks(const CMString& file_path);

    void broadcast_object_removed(const CMString& db_id, const CMString& object_name);

    // message 配额同步：把当前所有配额设置（全量快照）广播给所有在线 worker。
    // 由 set_limit_change_callback 触发（用户 set_*_limit 后）。
    void broadcast_message_limits();

    uint16_t get_port() const { return port_; }
    int32_t get_data_server_port() const { return data_server_port_; }

    void register_database(const CMString& db_id, const CMString& base_path, const CMString& data_path = "");
    bool is_db_frozen(const CMString& db_id) const;
    // 非 stream 模式 pending frozen 状态机（WP1）。
    // is_db_frozen 覆盖 confirmed ∪ pending（跨 task 写注册拦截）。
    // commit/rollback 按 task_id 精确迁移/清除 pending（task 成功迁移+广播，失败/崩溃回滚）。
    bool is_db_pending_frozen(const CMString& db_id) const;
    void commit_pending_frozen(uint64_t task_id);    // task 成功：pending→confirmed + 广播
    void rollback_pending_frozen(uint64_t task_id);  // task 失败/崩溃：按 task_id 清 pending
    // 消息处理入口（public 供测试直接调用，reactor 通过 lambda 调用）：
    void on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg);
    void on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg);
    CMSharedPtr<Database> get_or_create_database(const CMString& base_path, const CMString& data_path = "", uint64_t writer_id = 0);

    void setup_write_context();

    // load_db support methods
    CMVector<IndexEntry> restore_master_idx(const CMString& db_id, const CMString& base_path, const CMString& writer_id);
    // 轻量读 idx：只返回 entries 列表，不调 restore_entries（不灌 master local_idx）也不
    // mark_data_ready。用于 merge_db Phase 3 取对象清单派发 task，避免"先污染再清理"绕路。
    CMVector<IndexEntry> read_idx_entries(const CMString& base_path, const CMString& writer_id);
    void send_idx_load_commands(const CMString& db_id, const CMString& base_path, const CMVector<CMString>& writer_ids);
    void rebuild_remote_idx(const CMString& db_id, const CMString& base_path, const CMVector<::WorkerInfo>& workers);
    void send_idx_load_to_worker(const CMString& db_id, const CMString& base_path,
                                  const CMVector<CMString>& writer_ids, uint64_t worker_id);
    void rebuild_remote_idx_for_worker(const CMString& db_id, const CMString& base_path,
                                        const CMVector<CMString>& writer_ids, uint64_t worker_id);
    void set_master_hostname(const CMString& hostname);

    // ── DB Merge support (fly.merge_db 主动 API) ──────────────────────────
    // 派发单个 __merge_object internal task 给 target_worker：跨机拉源对象，
    // 落到 target_data_path（master host 本地）。返回派发的 task_id（用于 wait_merge_tasks_complete）。
    // 详见 docs/db-merge-design.md §3.4。
    uint64_t send_merge_task(uint64_t target_worker_id,
                              const CMString& short_name, const CMString& db_id,
                              const CMString& base_path, const CMString& target_data_path,
                              const CMString& source_host);
    // 命令 source_worker 删除本地 data_path 下的 .dat（merge 成功后清理源）。
    // data_path 显式传入（源 data_path），worker 不查 db_registry —— cleanup 会改
    // master 的 db_registry 到 merge 路径，db_registry 解析会拿错路径。
    void send_delete_data(uint64_t source_worker_id,
                           const CMString& db_id, const CMString& base_path,
                           const CMString& data_path,
                           const CMVector<CMString>& writer_ids);
    // 等待一批 DeleteData 的 ack 全部返回。返回是否全部成功；
    // 失败的 worker_id 在 failed_workers。wait 返回后 erase 对应 ack_key（防内存泄漏）。
    bool wait_delete_data_acks(const CMVector<uint64_t>& source_worker_ids,
                                const CMString& db_id,
                                int64_t timeout_seconds,
                                CMVector<uint64_t>* failed_workers = nullptr);
    // 等待一批 merge task 全部完成（TaskComplete/TaskFailed）。返回全部成功的对象名列表；
    // 失败的对象在 failed_objects。merge 的"全部成功才删源"语义依赖此同步点（设计 §5.4）。
    bool wait_merge_tasks_complete(const CMVector<uint64_t>& task_ids,
                                    int64_t timeout_seconds,
                                    CMVector<CMString>* completed_objects = nullptr,
                                    CMVector<CMString>* failed_objects = nullptr);
    // merge 全部成功后的状态清理：
    //  1. 广播 MergeCleanupMessage 给所有 worker，命令它们清 local_idx_[db_id]
    //     （exempt_worker_ids = merge target workers，保留它们有效的新 local_idx）
    //  2. 清 master 自身 DataService local_idx_[db_id]（restore_master_idx 灌入的源 entry）
    //  3. 清 master remote_idx_ 里指向源 worker 的 replica（保留 merge worker replica），
    //     避免首读试源 worker（源下线时卡 30s 网络超时）
    //  4. 更新 db_registry_[db_id] 指向 merge 路径（让后续 DbPathRequest 返回正确路径）
    // 不清 ObjectCache（数据内容未变，cache 是正确副本）。
    void cleanup_after_merge(const CMString& db_id,
                              const CMVector<CMString>& merged_object_full_names,
                              const CMVector<uint64_t>& source_worker_ids,
                              const CMVector<uint64_t>& merge_target_worker_ids,
                              const CMString& merge_base_path,
                              const CMString& merge_data_path);
    // merge task 完成/失败回调（由 on_task_complete / on_task_failed 的 internal 分支调用）。
    void on_merge_task_complete(uint64_t task_id, uint64_t worker_id, const CMVector<WrittenObject>& written_objects);
    void on_merge_task_failed(uint64_t task_id, const CMString& error_message);

private:
    CMString host_;
    uint16_t port_;
    int32_t data_server_port_ = 0;
    // Pool for master-initiated direct reads (TIER2). Mirrors worker's pool so
    // master read_object goes TIER1 → TIER2 (no TIER3: master is the location
    // authority).
    DataClientPool data_client_pool_{Config::instance()->get_int("data_client_pool_size")};
    std::atomic<bool> running_{false};

    std::atomic<bool> draining_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::mutex drain_mutex_;
    std::condition_variable drain_cv_;
    std::thread drain_thread_;

    CMUniquePtr<Reactor> reactor_;
    std::thread reactor_thread_;

    mutable std::mutex workers_mutex_;
    std::condition_variable workers_drained_cv_;  // Notified when all workers disconnect during shutdown.
    CMUnorderedMap<uint64_t, uint64_t> conn_to_worker_;
    CMUnorderedMap<uint64_t, uint64_t> worker_to_conn_;

    CMUniquePtr<DependencyGraph> graph_;
    CMUniquePtr<WorkerManager> worker_manager_;
    CMUniquePtr<TaskScheduler> scheduler_;
    CMUniquePtr<TaskManager> metadata_;
    CMUniquePtr<HeartbeatMonitor> heartbeat_monitor_;
    std::thread heartbeat_check_thread_;
    std::atomic<bool> heartbeat_check_running_{false};
    std::mutex heartbeat_check_mutex_;
    std::condition_variable heartbeat_check_cv_;

    // Attribute timeout 检查线程：周期性触发 schedule_tasks，
    // 让限时等待属性的 task 在超时后被降级调度。
    std::thread attr_timeout_check_thread_;
    std::atomic<bool> attr_timeout_check_running_{false};
    std::mutex attr_timeout_check_mutex_;
    std::condition_variable attr_timeout_check_cv_;

    CMUnorderedMap<uint64_t, CMString> task_modules_;
    CMUnorderedMap<uint64_t, CMVector<CMString>> task_args_;
    // Declared var names per task (from @as_task(vars=...)).
    CMUnorderedMap<uint64_t, CMVector<CMString>> task_vars_;
    mutable std::mutex task_args_mutex_;

    // Pre-fetched dependency locations: task_id → {object_name → (worker_id, host, port)}.
    // Updated on write_register, consumed on assign_task_to_worker.
    struct CachedLocation {
        uint64_t worker_id = 0;
        CMString host;
        int32_t port = 0;
    };
    CMUnorderedMap<uint64_t, CMUnorderedMap<CMString, CachedLocation>> task_dependency_locations_;
    mutable std::mutex dep_loc_mutex_;

    CMUnorderedMap<CMString, CMUnorderedMap<CMString, CMString>> db_registry_;
    CMUnorderedMap<CMString, CMSharedPtr<Database>> db_instances_;
    CMUnorderedSet<CMString> frozen_dbs_;
    // 非 stream 模式 pending frozen：db_id → task_id（待 task 完成确认）。
    // task 内 freeze 时登记 pending（拒其他 task 写，但不广播）；task 成功迁移到
    // frozen_dbs_ + 广播，task 失败/崩溃按 task_id 回滚清除（防永久死锁）。
    CMUnorderedMap<CMString, uint64_t> pending_frozen_dbs_;
    mutable std::mutex frozen_dbs_mutex_;
    static std::atomic<uint64_t> remote_task_counter_;

    // ── Merge task 跟踪（fly.merge_db）──────────────────────────────────
    // 每个 merge __merge_object task 的状态，由 on_merge_task_complete/Failed 更新，
    // wait_merge_tasks_complete 等待。设计 §5.4：全部成功才删源。
    struct MergeTaskState {
        bool completed_ = false;
        bool success_ = false;
        CMString error_message_;
        CMVector<CMString> written_objects_;  // 成功时填入（full_name 列表）
        uint64_t worker_id_ = 0;  // 执行 merge task 的 worker（精确的对象持有者）
    };
    CMUnorderedMap<uint64_t, MergeTaskState> merge_task_states_;
    mutable std::mutex merge_task_mutex_;
    std::condition_variable merge_task_cv_;

    // ── DeleteData ack 跟踪（merge 删源）──────────────────────────────
    // key = (db_id + ":" + worker_id) 的字符串，避免多 worker/db 并发删除时 ack 串台。
    struct PendingDeleteData {
        bool completed_ = false;
        bool success_ = false;
        int32_t deleted_count_ = 0;
        CMString error_message_;
    };
    CMUnorderedMap<CMString, PendingDeleteData> pending_delete_acks_;
    mutable std::mutex delete_ack_mutex_;
    std::condition_variable delete_ack_cv_;

    // ── MergeCleanup ack 跟踪（merge_db 返回前的全局一致性屏障）──
    // master 广播 MergeCleanup 后，必须等所有 worker 回 ack 才能重建自身 remote_idx +
    // 让 merge_db 返回。key = db_id（一次 merge_db 的 cleanup 是单 db 全员广播）。
    struct PendingMergeCleanup {
        uint64_t expected_count_ = 0;   // 期望的 ack 数（广播时的 worker 数）
        uint64_t received_count_ = 0;   // 已收到的 ack 数
    };
    CMUnorderedMap<CMString, PendingMergeCleanup> pending_merge_cleanups_;
    mutable std::mutex merge_cleanup_mutex_;
    std::condition_variable merge_cleanup_cv_;

    // ── Message summary 屏障（进程结束前收集各 worker 的 message 触发次数）──
    // master stop() 广播 MSG_COUNT_REQUEST 后，等所有 worker 回 MSG_COUNT_REPORT。
    // 复刻 MergeCleanupAck 的计数屏障模式（expected/received + cv）。
    struct PendingMsgCount {
        uint64_t expected_count_ = 0;
        uint64_t received_count_ = 0;
    };
    PendingMsgCount pending_msg_count_;
    CMVector<std::pair<uint64_t, MessageCounts>> collected_msg_counts_;
    mutable std::mutex msg_count_mutex_;
    std::condition_variable msg_count_cv_;

    void schedule_tasks();
    void assign_task_to_worker(uint64_t task_id, uint64_t worker_id);
    void update_dependency_location_cache(const CMString& object_name, uint64_t worker_id, const CMString& host, int32_t port);
    void heartbeat_check_loop();
    void attr_timeout_check_loop();

    std::mutex schedule_mutex_;

    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);
    void on_data_query_dispatch(uint64_t conn_id, const DataQueryMessage& msg);
    void on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg);
    WriteRegisterAckMessage do_write_register(const WriteRegisterMessage& msg);
    void record_worker_info(const CMString& object_name, const CMString& db_id,
                            uint64_t worker_id, const CMString& writer_id);
    void evaluate_and_trigger_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_id);
    void on_worker_property_update(uint64_t conn_id, const WorkerPropertyUpdateMessage& msg);
    void on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg);
    void on_remove_request(uint64_t conn_id, const RemoveRequestMessage& msg);
    void on_database_freeze_request(uint64_t conn_id, const DatabaseFreezeNotification& msg);
    void on_idx_load_ack(uint64_t conn_id, const IdxLoadAckMessage& msg);
    void on_delete_data_ack(uint64_t conn_id, const DeleteDataAckMessage& msg);
    void on_merge_cleanup_ack(uint64_t conn_id, const MergeCleanupAckMessage& msg);

    // Message 日志系统 handlers。
    void on_log_message(uint64_t conn_id, const LogMessage& msg);
    void on_message_count_report(uint64_t conn_id, const MessageCountReportMessage& msg);
    // 进程结束前收集各 worker 的 message 计数并打印 summary（stop Phase 内调用）。
    void collect_and_print_message_summary();

    // Var service handlers.
    void on_var_set(uint64_t conn_id, const VarSetMessage& msg);
    void on_var_get(uint64_t conn_id, const VarGetMessage& msg);
    void on_var_remove(uint64_t conn_id, const VarRemoveMessage& msg);
    void broadcast_var(const CMString& full_var_name, bool is_modification_reject);

    void on_backup_request(uint64_t conn_id, const BackupRequestMessage& msg);
    uint64_t select_backup_worker(uint64_t source_worker_id);

    void trigger_auto_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_id);

    void persist_failed_task(const FailedTaskRecord& record);
    void remove_persisted_task(uint64_t task_id);
    CMString get_failed_tasks_file_path() const;

    void on_master_freeze(const CMString& db_id);
    std::pair<CMString, TaskErrorType> on_master_register_write(const CMString& db_id, const CMString& name, int64_t compressed_size);

    std::atomic<bool> fatal_error_{false};

    CMUnorderedMap<uint64_t, CMString> worker_to_hostname_;
    CMUnorderedMap<uint64_t, CMString> worker_to_ip_;
    CMUnorderedSet<std::tuple<CMString, CMString, CMString>> recorded_workers_;
    mutable std::mutex recorded_workers_mutex_;

    CMUnorderedMap<CMString, CMString> write_provenance_;
    mutable std::mutex provenance_mutex_;

    static std::atomic<bool> sigterm_received_;
    static void sigterm_handler(int sig);

    void check_shutdown_request();
    void do_drain_and_stop();
    void persist_pending_tasks();
    FailedTaskRecord build_failed_record(uint64_t task_id);
    void notify_drain_if_active();
};

}  // namespace fly
