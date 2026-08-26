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
#include <agent/cpp/pending_rpc_map.h>
#include <agent/cpp/run_metrics.h>
#include <monitor/cpp/metrics_db.h>
#include <monitor/cpp/monitor_sampler.h>
#include <core/cpp/config.h>
#include <common/cpp/common_types.h>
#include <common/cpp/concurrent_map.h>
#include <common/cpp/worker_context.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <signal.h>
#include <memory>
#include <functional>
#include <utility>

namespace fly {

// FailedTaskRecord — 持久化到 failed_tasks.bin 的失败 task 记录，供
// restart_failed_tasks 读回重新提交。内嵌 TaskSubmissionSpec 复用提交字段，
// 新增提交字段时自动随 spec 持久化（无需手动同步 FLY_SERIALIZE 列表）。
struct FailedTaskRecord {
    uint64_t task_id_ = 0;
    TaskSubmissionSpec submission_;    // 提交时的完整不变字段（restart 还原用）
    CMString error_message_;

    FLY_SERIALIZE(task_id_, submission_, error_message_);
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
    // 正常收尾停止（脚本执行完毕后自动调用）：drain 等待全部 RUNNING task 完成
    // （无硬 deadline；兜底=心跳判死链 + 断连宽限超时；可被 fast_exit 打断），
    // 然后广播 ShutdownMessage（worker 优雅退：flush coverage + WBQ drain）。
    void stop();
    // 快速退出（SIGTERM / graceful_exit 致命错误通道）：不等待 task——立即对全部
    // RUNNING task fail 善后（fail_task + failed record 持久化），广播 StopNowMessage
    // （worker 收到即 kill 自身，不依赖 master 知晓 pid/句柄），短宽限等断连后收尾。
    // 与 stop() 并发时：若 stop() 已在 drain，置打断标志使其转快速路径。
    void fast_exit(const CMString& reason = "fast exit");
    bool is_running() const;
    // SIGTERM 优雅退出入口（heartbeat 线程消费信号灯后调用；单测直接调用）。
    // 独立线程执行 fast_exit（快速退出语义），幂等。
    void trigger_graceful_shutdown();

    CMVector<uint64_t> get_connected_workers() const;
    CMVector<std::pair<uint64_t, CMString>> get_worker_hostnames() const;
    // storage_only worker 的 worker_id 快照（Python 侧识别存储节点用）。
    CMVector<uint64_t> get_storage_only_workers() const;
    void add_worker_hostname(uint64_t worker_id, const CMString& hostname,
                             WorkerRole role = WorkerRole::HYBRID);
    size_t get_connection_count() const;

    // ── expected workers（唤起占位符，bsub 等慢调度场景）──────────────
    // master 尝试唤起 worker 后登记占位符；RegisterMessage 到达即转正（移除）。
    // 占位符不参与调度、不进连接表——调度/心跳/stop 只认已注册 worker。
    // 默认（worker_register_timeout=0）不假设任何注册时限；>0 时超时占位符由
    // heartbeat 检查线程清理并 WARN。
    void expect_worker(uint64_t worker_id);
    size_t get_expected_worker_count() const;
    bool all_workers_registered() const;

    // submit_task 接收完整的 TaskSubmissionSpec，避免 11 个位置参数导致的
    // 错位/漏传（位置参数同类，编译器无法捕获）。调用方先组装 spec 再传入。
    void submit_task(uint64_t task_id, const TaskSubmissionSpec& spec);
    // 位置参数便利重载：内部组装 spec 转发到上面的主签名。生产代码应优先
    // 用 spec 形式；此重载主要服务测试与少量不便组装 spec 的调用点。
    void submit_task(uint64_t task_id, const CMString& name,
                    const CMString& module, const CMVector<CMString>& args,
                    const CMVector<CMString>& inputs = {},
                    const CMVector<CMString>& outputs = {},
                    const CMVector<CMString>& required_capabilities = {},
                    float attribute_timeout = -1.0f,
                    const CMString& write_context_hash = "",
                    const CMVector<CMString>& vars = {},
                    int priority = 10,
                    const CMString& owner_db_path = "");

    CMVector<uint64_t> get_pending_tasks() const;
    CMVector<uint64_t> get_running_tasks() const;
    CMVector<uint64_t> get_completed_tasks() const;
    CMVector<uint64_t> get_failed_tasks() const;
    CMString get_task_error(uint64_t task_id) const;

    CMVector<uint64_t> get_idle_workers() const;

    // 断点重投：读单个 failed_tasks.bin（读取+删除原子，重投走 submit_task，
    // 归属随 submission_ 还原）。返回重启的 task 条数。db list 形态的自动搜索
    // 由 Python 层归一化后逐个调用（fly.restart_failed_tasks / Project.resume）。
    size_t restart_failed_tasks(const CMString& file_path);

    void broadcast_object_removed(const CMString& db_path, const CMString& object_name);

    // message 配额同步：把当前所有配额设置（全量快照）广播给所有在线 worker。
    // 由 set_limit_change_callback 触发（用户 set_*_limit 后）。
    void broadcast_message_limits();

    uint16_t get_port() const { return port_; }
    int32_t get_data_server_port() const { return data_server_port_; }

    // 登记一个【外部已知 db_path】的 db 路径（master 自写用 get_or_create_database 构造；
    // 此处用于 load/merge 等已从 _DB_META 读出 db_path 的场景）。Database 是路径唯一权威源，
    // 故内部构造 Database 插入 db_instances_（替代原 db_registry_ 字符串副本）。
    void register_database(const CMString& db_path, const CMString& data_path = "");
    // 诊断：返回某 db 的 provenance 条目数（测试验证 freeze 清理 / load 重建用）。
    size_t provenance_count_for_testing(const CMString& db_path) const;
    // RunMetricsCollector 透传（测试断言样本接收用；生命周期同 MasterAgent）。
    RunMetricsCollector* run_metrics_for_testing() const { return run_metrics_.get(); }
    // MetricsDb 透传（monitor 落盘层；未启用 monitor_db_enabled 时 opened()=false）。
    MetricsDb* metrics_db_for_testing() const { return metrics_db_.get(); }
    bool is_db_frozen(const CMString& db_path) const;
    // 非 stream 模式 pending frozen 状态机（WP1）。
    // is_db_frozen 覆盖 confirmed ∪ pending（跨 task 写注册拦截）。
    // commit/rollback 按 task_id 精确迁移/清除 pending（task 成功迁移+广播，失败/崩溃回滚）。
    bool is_db_pending_frozen(const CMString& db_path) const;
    void commit_pending_frozen(uint64_t task_id);    // task 成功：pending→confirmed + 广播
    void rollback_pending_frozen(uint64_t task_id);  // task 失败/崩溃：按 task_id 清 pending
    // 消息处理入口（public 供测试直接调用，reactor 通过 lambda 调用）：
    void on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg);
    void on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg);
    // 心跳处理（保活 + 判死数据更新，测试直调验证）。
    void on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg);
    // monitor 采样成组上报处理（RunMetrics 喂样 + MetricsDb 落库；测试直调）。
    void on_monitor_sample(const MonitorSampleMessage& msg);
    // task 对象级 IO 明细上报（object_io 表落库；测试直调）。
    void on_task_io_report(const MonitorTaskIoMessage& msg);

    // ---- cluster monitor 落盘接线（全部非阻塞入队）----
    // master 内存态 → TaskRow 基础快照（状态/时间戳/worker/dbs 解析）。
    TaskRow build_task_row(uint64_t task_id) const;
    // 快照落库（submit/assign/REQUEUE 等无 worker 扩展字段的事件点）。
    void record_task_snapshot(uint64_t task_id);
    // master 自监控采样线程体（MonitorSampler 直写 worker_id=0 行）。
    void monitor_self_loop();
    // master 侧事件驱动采样（task 完成/worker 注册/收到 worker 样本等 cluster
    // 事件时刻的全维度快照，kind=1 直写 DB；节流与自监控周期共用）。
    void monitor_self_event();
    // db 磁盘占用落库（DB_DU 事件，detail="<path>|<bytes>"）：freeze 点与
    // stop 收尾各落一次（DBs 页磁盘占用数据源）。
    void record_db_du(const CMString& db_path);
    std::thread monitor_self_thread_;
    std::atomic<bool> monitor_self_running_{false};
    std::mutex monitor_self_mutex_;
    std::condition_variable monitor_self_cv_;
    MonitorSampler monitor_self_sampler_;  // 周期 + 事件采样共用（内部互斥）
    std::mutex self_sample_throttle_mutex_;
    uint64_t self_last_sample_ms_ = 0;     // 事件采样节流基准
    int64_t self_sample_gap_ms_ = 200;     // 最小间距（start 时读 config）
    // 同步写注册裁决（on_write_register 的核心，无网络副作用前半段）——
    // public 供测试直调失败分类（frozen/mismatch/空 hash REGISTRATION_FAILED）。
    WriteRegisterAckMessage do_write_register(const WriteRegisterMessage& msg);
    // 调度编排入口（submit/complete 等多处触发）——public 供测试直调
    //（并发调度与判死检测的交错回归，见 UnresolvableDetectionDoesNotFireDuringAssignFlight）。
    void schedule_tasks();
    CMSharedPtr<Database> get_or_create_database(const CMString& db_path, const CMString& data_path = "", uint64_t writer_id = 0);
    // 取 db_instances_ 里的权威 Database（load_db/merge 复用，避免 Python 端再构造一个
    // 会触发 DataService::unregister 析构副作用的临时 Database）。miss 返回 nullptr。
    CMSharedPtr<Database> get_database(const CMString& db_path) const;

    void setup_write_context();

    // load_db support methods
    CMVector<IndexEntry> restore_master_idx(const CMString& db_path, const CMString& writer_id);
    // 轻量读 idx：只返回 entries 列表，不调 restore_entries（不灌 master local_idx）也不
    // mark_data_ready。用于 merge_db Phase 3 取对象清单派发 task，避免"先污染再清理"绕路。
    CMVector<IndexEntry> read_idx_entries(const CMString& db_path, const CMString& writer_id);
    void send_idx_load_commands(const CMString& db_path, const CMVector<CMString>& writer_ids);
    void rebuild_remote_idx(const CMString& db_path, const CMVector<::WorkerInfo>& workers);
    void send_idx_load_to_worker(const CMString& db_path,
                                  const CMVector<CMString>& writer_ids, uint64_t worker_id);
    void rebuild_remote_idx_for_worker(const CMString& db_path,
                                        const CMVector<CMString>& writer_ids, uint64_t worker_id);
    // load_db 可见性屏障查询：db_path 的待完成 IdxLoadAck 数（0=全部 Ack+rebuild
    // 完成，对象位置已可见；-1=有 Ack 失败；未发送过命令的 db 恒 0）。Python 轮询。
    int32_t idx_load_pending(const CMString& db_path);
    void set_master_hostname(const CMString& hostname);

    // ── DB Merge support (fly.merge_db 主动 API) ──────────────────────────
    // 派发单个 __merge_object internal task 给 target_worker：跨机拉源对象，
    // 落到 target_data_path（master host 本地）。返回派发的 task_id（用于 wait_merge_tasks_complete）。
    // 详见 docs/db-merge-design.md §3.4。
    uint64_t send_merge_task(uint64_t target_worker_id,
                              const CMString& short_name,
                              const CMString& source_db_path,
                              const CMString& target_db_path,
                              const CMString& target_data_path,
                              const CMString& source_host);
    // 命令 source_worker 删除本地 data_path 下的 .dat（merge 成功后清理源）。
    // data_path 显式传入（源 data_path），worker 不查 db_registry —— cleanup 会改
    // master 的 db_registry 到 merge 路径，db_registry 解析会拿错路径。
    void send_delete_data(uint64_t source_worker_id,
                           const CMString& db_path,
                           const CMString& data_path,
                           const CMVector<CMString>& writer_ids);
    // 等待一批 DeleteData 的 ack 全部返回。返回是否全部成功；
    // 失败的 worker_id 在 failed_workers。wait 返回后 erase 对应 ack_key（防内存泄漏）。
    bool wait_delete_data_acks(const CMVector<uint64_t>& source_worker_ids,
                                const CMString& db_path,
                                int64_t timeout_seconds,
                                CMVector<uint64_t>* failed_workers = nullptr);
    // 等待一批 merge task 全部完成（TaskComplete/TaskFailed）。返回全部成功的对象名列表；
    // 失败的对象在 failed_objects。merge 的"全部成功才删源"语义依赖此同步点（设计 §5.4）。
    bool wait_merge_tasks_complete(const CMVector<uint64_t>& task_ids,
                                    int64_t timeout_seconds,
                                    CMVector<CMString>* completed_objects = nullptr,
                                    CMVector<CMString>* failed_objects = nullptr);
    // merge 全部成功后的状态清理：
    //  1. 广播 MergeCleanupMessage 给所有 worker，命令它们清 local_idx_[db_path]
    //     （exempt_worker_ids = merge target workers，保留它们有效的新 local_idx）
    //  2. 清 master 自身 DataService local_idx_[db_path]（restore_master_idx 灌入的源 entry）
    //  3. 清 master remote_idx_ 里指向源 worker 的 replica（保留 merge worker replica），
    //     避免首读试源 worker（源下线时卡 30s 网络超时）
    //  4. 更新 db_instances_[db_path] 的 Database 路径（set_paths）指向 merge 路径，
    //     让后续 DbPathRequest 返回正确路径（Database 现是 master 进程路径唯一权威源）。
    // 不清 ObjectCache（数据内容未变，cache 是正确副本）。
    // merge_db 失败路径清理：按 db_path 精确清 merge_task_states_ + 广播 purge
    //（MergeCleanupMessage.purge_target_=true：源命名空间全保留，持有该 target
    // merge writer 的 worker 删除自己的产物 .dat/.idx）。best-effort，不等屏障。
    void cleanup_failed_merge(const CMString& db_path,
                               const CMString& merge_db_path,
                               const CMString& merge_data_path);
    void cleanup_after_merge(const CMString& db_path,
                              const CMVector<CMString>& merged_object_full_names,
                              const CMVector<uint64_t>& source_worker_ids,
                              const CMVector<uint64_t>& merge_target_worker_ids,
                              const CMString& merge_db_path,
                              const CMString& merge_data_path);
    // merge task 完成/失败回调（由 on_task_complete / on_task_failed 的 internal 分支调用）。
    void on_merge_task_complete(uint64_t task_id, uint64_t worker_id, const CMVector<WrittenObject>& written_objects);
    void on_merge_task_failed(uint64_t task_id, const CMString& error_message);

    // ── Auto-backup EWMA 聚合（worker suggest → master score → 判定 backup）──
    // master 聚合多 worker 上报的 TIER2 读增量，按 suggest 接收时间做 EWMA 衰减。
    // score = cumulative / replicas；backup → replicas++ → score 降 → 自然平衡（不 reset）。
    struct ObjectBackupScore {
        double cumulative_bytes_ = 0;   // EWMA 衰减后的累积字节
        double cumulative_count_ = 0;   // EWMA 衰减后的累积次数
        int64_t size_bytes_ = 0;        // 对象最新压缩后大小（大文件例外判定用）
        int64_t last_suggest_time_ = 0; // 上次 suggest 到达时间（EWMA elapsed 用）
    };

#ifdef FLY_ENABLE_TEST_HOOKS
public:
    // ── 测试专用接口：仅当编译期定义 FLY_ENABLE_TEST_HOOKS 时存在 ──
    // release（fly_agent / fly_agent_so / fly 二进制）永不定义该宏 → 这些成员、
    // 触发点与 helper 完全不出现在发布产物。仅 testonly 库变体 fly_agent_test_hooks 激活。
    //
    // 用于确定性复现 task 生命周期跨线程竞态：测试用 std::latch 在钩子点协调线程交错。
    //   assign_task_send_hook_for_testing_         — assign_task_to_worker 在 reactor_->send
    //                                                之后、metadata_/worker_manager 赋值之前触发
    //                                                （scheduler 线程，持 schedule_mutex_）。
    //   on_task_complete_prelock_hook_for_testing_ — on_task_complete 在获取 schedule_mutex_
    //                                                之前触发（结构稳定点，不随 complete_task
    //                                                位置移动；complete_task 在其上方/下方决定
    //                                                竞态方向）。
    std::function<void(uint64_t task_id, uint64_t worker_id)> assign_task_send_hook_for_testing_;
    std::function<void(uint64_t task_id, uint64_t worker_id)> on_task_complete_prelock_hook_for_testing_;
    // on_disconnect 入口触发（handler lane 线程，进入任何清理前）。测试用它
    // 阻塞旧连接的断连处理，确定性构造「重连 REGISTER 先于旧 conn DISCONNECT
    // 被 master 处理」的 lane 并行交错（P3-26 回归用）。
    std::function<void(uint64_t conn_id)> on_disconnect_entry_hook_for_testing_;
    // 注册一个不对应真实网络连接的 worker（fake_conn_id），使 assign_task_to_worker 的
    // reactor_->send 安全 no-op（transport 对未知 conn_id 返回 -1）。用于无需真实 worker
    // 进程即可驱动调度路径的确定性测试（消除真实 worker 异步完成对断言的干扰）。
    void register_fake_worker_for_testing(uint64_t worker_id, uint64_t fake_conn_id);
    // 撤销 register_fake_worker_for_testing 的登记（清 workers map + worker_manager 条目），
    // 使 master.stop() 的 drain 不必为永不真实断连的 fake worker 等待 10s 超时。
    void unregister_fake_worker_for_testing(uint64_t worker_id, uint64_t fake_conn_id);
    // pending ack/cleanup 状态只读访问（复现 Problem 5 静默覆盖）。
    // delete：返回 {completed, deleted_count}；merge cleanup：返回 {expected, received}。
    std::pair<bool, int32_t> pending_delete_ack_state_for_testing(const CMString& ack_key) const;
    std::pair<uint64_t, uint64_t> pending_merge_cleanup_counts_for_testing(const CMString& db_path) const;
    // 直接注入 DeleteDataAck（绕过 reactor），驱动 pending 状态机用于测试。
    void inject_delete_data_ack_for_testing(const DeleteDataAckMessage& msg) { on_delete_data_ack(0, msg); }
    // 直接驱动 select_backup_worker（private），验证 host 级分散选择用于测试。
    uint64_t select_backup_worker_for_testing(const CMString& object_name) { return select_backup_worker(object_name); }
    // ── auto_backup EWMA 判定测试钩子（2026-08-16 补覆盖：此前 master 侧判定零测试）──
    // 直接驱动 on_worker_backup_suggest（conn_id=0，消息路径不依赖真实连接）。
    void worker_backup_suggest_for_testing(const WorkerBackupSuggestMessage& msg) {
        on_worker_backup_suggest(0, msg);
    }
    // score 状态只读观测（EWMA 累积/size/last_suggest_time 断言用）。
    // ObjectBackupScore 完整定义见下方 private 区（此处前向声明不可行：访问性冲突）。
    ObjectBackupScore backup_score_for_testing(const CMString& object_name) const;
    // trigger_auto_backup 进入次数（无论后续选目标是否成功）——判定分支的触发观测。
    uint64_t auto_backup_trigger_count_for_testing_ = 0;
    // 吞掉下一条 REGISTER（不处理、不回 ack）——构造「注册/ack 丢失」的
    // 确定性场景（P3-23 重发兜底单测用）。
    std::atomic<bool> drop_next_register_for_testing_{false};
    // 诊断：指定 db_path 的 merge task 状态条目数（失败清理精确性测试用）。
    size_t merge_task_state_count_for_testing(const CMString& db_path) const;
    // 直接驱动 record_worker_info（private），验证同 tuple 只 append meta 一次。
    void record_worker_info_for_testing(const CMString& object_name, const CMString& db_path,
                                        uint64_t worker_id, const CMString& writer_id) {
        record_worker_info(object_name, db_path, worker_id, writer_id);
    }
    // 直接驱动 on_worker_register / 占位符超时清理（private），确定性测试注册
    // 转正与超时清理逻辑（无需真实网络与 heartbeat 线程周期）。
    void inject_worker_register_for_testing(uint64_t conn_id, const RegisterMessage& msg) {
        on_worker_register(conn_id, msg);
    }
    // 直接驱动 on_idx_load_ack（private），load_db 可见性屏障状态机测试用。
    void inject_idx_load_ack_for_testing(const IdxLoadAckMessage& msg) {
        on_idx_load_ack(0, msg);
    }
    void check_expected_worker_timeouts_for_testing(int64_t now) {
        check_expected_worker_timeouts(now);
    }
    void check_grace_deadlines_for_testing(int64_t now) {
        check_grace_deadlines(now);
    }
    // failed_tasks 路径解析确定性测试：按 owner 解析路径 + persist 落点。
    CMString failed_tasks_file_path_for_testing(const CMString& owner_db_path) const {
        return get_failed_tasks_file_path(owner_db_path);
    }
    // 重投后 task 的归属查询（位置即归属归一化的确定性测试）。
    CMString task_owner_db_path_for_testing(uint64_t task_id) const {
        auto md = metadata_->get_task(task_id);
        return md ? md->submission_.owner_db_path_ : CMString();
    }
    void persist_failed_task_for_testing(const FailedTaskRecord& record) {
        persist_failed_task(record);
    }
    // 直接驱动 handle_worker_death（private）：判死联动收敛（pending RPC 期待
    // 终结）的确定性测试入口，无需构造宽限超时路径。
    void handle_worker_death_for_testing(uint64_t worker_id) {
        handle_worker_death(worker_id);
    }
    // 宽限表快照：测试等待"断连已进宽限登记"的确定性条件。on_disconnect 的
    // 清表（connected 不可见）与宽限登记之间有中间代码，负载下 lane 线程可
    // 在该窗口被抢占——等 connected empty 就 check 会空转（判死未发生）。
    // with_lock 非 const，hook 不加 const 限定。
    CMVector<uint64_t> grace_workers_for_testing() {
        CMVector<uint64_t> workers;
        grace_deadlines_.with_lock([&](const auto& m) {
            for (const auto& [wid, deadline] : m) workers.push_back(wid);
        });
        return workers;
    }
    // 存储接管测试用：驱动判死后的接管决策（无网络注册流程下直接构造拓扑）。
    bool try_storage_takeover_for_testing(uint64_t worker_id) {
        return try_storage_takeover(worker_id);
    }
    // 接管超时兜底测试用：驱动 deadline 检查（补做 fail_orphan_data_objects）。
    void check_takeover_deadlines_for_testing(int64_t now) {
        check_takeover_deadlines(now);
    }
    // 自动补齐测试用：驱动检测循环（绕过节流；注册/spawn 链路走真网络）。
    void check_storage_nodes_for_testing(int64_t now) {
        last_storage_check_ts_ = 0;
        check_storage_nodes(now);
    }
    size_t pending_storage_spawns_for_testing() {
        return pending_storage_spawns_.size();
    }
    // 检测决策命中计数（决定对某 host 发起 spawn 的次数，与发送成败解耦
    //——单测无网络连接也能断言决策）。
    int64_t storage_spawn_decisions_for_testing() {
        return storage_spawn_decisions_.load();
    }
    // 接管测试用：注入 recorded_workers_ 条目（绕过 _DB_META 落盘链路）。
    void insert_recorded_worker_for_testing(const CMString& db, const CMString& host,
                                             const CMString& writer) {
        recorded_workers_.insert(std::make_tuple(db, host, writer));
    }
    int64_t takeover_load_for_testing(uint64_t worker_id) {
        return takeover_load_.find(worker_id).value_or(0);
    }
    size_t takeover_pending_size_for_testing() {
        return takeover_pending_.size();
    }
    // 数据全灭测试用：驱动 graph 的 mark_data_ready（让依赖 task 进入可调度状态）。
    void mark_data_ready_for_testing(const CMString& object_name) {
        graph_->mark_data_ready(object_name);
    }
private:
#endif

private:
    CMString host_;
    uint16_t port_;
    // 构造时请求的监听端口：start() 每次用它 bind（port 0 = 每次拿全新临时端口）。
    // 不缓存上次绑定结果——重启复用旧端口会在 close→rebind 窗口内被其他进程抢作
    // 临时源端口，SO_REUSEADDR 拦不住活跃连接（-j4 高并发 QA 实测 EADDRINUSE）。
    uint16_t listen_port_ = 0;
    int32_t data_server_port_ = 0;
    // Pool for master-initiated direct reads (TIER2). Mirrors worker's pool so
    // master read_object goes TIER1 → TIER2 (no TIER3: master is the location
    // authority).
    DataClientPool data_client_pool_{Config::instance()->get_int("data_client_pool_size")};
    std::atomic<bool> running_{false};

    std::atomic<bool> draining_{false};
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<bool> graceful_stop_started_{false};
    // fast_exit 请求标志：打断 stop() 的 drain 等待（SIGTERM/致命错误到达时，
    // 正在优雅等待的 drain 转快速路径——先 fail 善后再走 StopNow 广播）。
    std::atomic<bool> fast_exit_requested_{false};
    std::mutex drain_mutex_;
    std::condition_variable drain_cv_;
    // stop()/fast_exit() 的统一实现（fast=true 跳过 drain 等待 + fail 善后 +
    // StopNow 广播 + 短宽限断连）。防重入：draining_ 首个置位者负责执行。
    void stop_impl(bool fast, const CMString& reason);

    CMUniquePtr<Reactor> reactor_;
    std::thread reactor_thread_;

    mutable std::mutex workers_mutex_;
    std::condition_variable workers_drained_cv_;  // Notified when all workers disconnect during shutdown.
    CMUnorderedMap<uint64_t, uint64_t> conn_to_worker_;
    CMUnorderedMap<uint64_t, uint64_t> worker_to_conn_;

    // 唤起占位符：worker_id → spawn 时间戳（epoch 秒）。注册到达转正（erase），
    // 超时清理见 check_expected_worker_timeouts。重复 expect 刷新时间戳。
    ConcurrentUnorderedMap<uint64_t, int64_t> expected_worker_ids_;

    // 断连宽限表：worker_id → 判死截止时间（epoch 秒）。断连（网络闪断）时登记
    //（worker_reconnect_timeout>0 才启用；宽限内 task 存活、不重调度、豁免心跳
    // 判死），由 heartbeat 检查线程扫描超时 → handle_worker_death；宽限内重连
    // 注册则 erase。0=断连即死（逃生口）不登记。
    ConcurrentUnorderedMap<uint64_t, int64_t> grace_deadlines_;
    // 存储接管 pending：死 worker_id → fail 兜底 deadline（接管发起时登记；
    // ack 的 mark_data_ready 恢复等待 task，deadline 到点幂等重判全灭与否）。
    ConcurrentUnorderedMap<uint64_t, int64_t> takeover_pending_;
    // 各 storage worker 累计接管的 writer 数（storage_takeover_max_writers
    // 上限依据；master 内存态，重启后 load_db 全量重建自然归零）。
    ConcurrentUnorderedMap<uint64_t, int64_t> takeover_load_;
    // 自动补齐（auto_storage_nodes_enabled）：host → spawn 占位 deadline
    //（防检测周期内重复 spawn；超时清除允许重试，storage 注册到达亦清除）。
    ConcurrentUnorderedMap<CMString, int64_t> pending_storage_spawns_;
    // host → spawn 连续失败计数（Ack 失败累积，>=3 WARN 放弃该 host；
    // storage 上线或集群状态变化时清零）。
    ConcurrentUnorderedMap<CMString, int64_t> storage_spawn_failures_;
    // 疑似重复注册的活性确认：worker_id → ProbeAck 时间戳（30s 新鲜度）。
    // 连接断开时清除；用于区分「旧实例活着（拒绝后到者）」与「旧 conn 是
    // EOF 未处理的残留（放行重连注册）」。
    ConcurrentUnorderedMap<uint64_t, int64_t> dup_confirmed_alive_;
    // 疑似重复注册的挂起（探测期间暂存后到者）：worker_id → 新 conn + 注册
    // 消息 + 结论 deadline。旧连接断开 → 重放注册（正常接受）；ProbeAck →
    // 拒绝；deadline → 保守拒绝。首连的 Register 无重发机制，闭环由 master
    // 完成（重放），不依赖 worker 超时重试。
    struct DeferredRegister {
        uint64_t conn_id_ = 0;
        RegisterMessage msg_;
        int64_t deadline_ = 0;
    };
    ConcurrentUnorderedMap<uint64_t, DeferredRegister> deferred_registers_;
    // 自动补齐分配的 worker_id 高基区自增（避开 Python launch 从 1 起的
    // 低位序列，两序列永不冲突）。
    std::atomic<uint64_t> next_auto_spawn_worker_id_{100000};
    // 自动补齐检测节流时间戳（heartbeat_check_loop 每次 5s，按
    // auto_storage_check_interval 节流）。
    std::atomic<int64_t> last_storage_check_ts_{0};
    // 检测决策命中计数（含 send 失败的决策；诊断/测试用）。
    std::atomic<int64_t> storage_spawn_decisions_{0};

    CMUniquePtr<DependencyGraph> graph_;
    CMUniquePtr<WorkerManager> worker_manager_;
    CMUniquePtr<TaskScheduler> scheduler_;
    CMUniquePtr<TaskManager> metadata_;
    CMUniquePtr<HeartbeatMonitor> heartbeat_monitor_;
    // 运行时指标采集（RunSummary）：集群内存快照 tick / db 窗口 / 磁盘用量，
    // stop 时写 runtime.summary + db.summary 并在用户日志打文件地址与总耗时
    // （详见 run_metrics.h）。worker RSS 样本改经 monitor 通道（on_monitor_sample）
    // 喂入，心跳不再携带采样数据。
    CMUniquePtr<RunMetricsCollector> run_metrics_;
    // cluster monitor 落盘层（master 单写 SQLite）：task/worker/db 事件全景 +
    // 负载采样时序，写 {log_dir}/monitor.db。start 时 open、stop 内同步 close
    // （flush 全部余量，早于 Logger/静态析构——P3-18 退出期时序）。
    CMUniquePtr<MetricsDb> metrics_db_;
    // 本 run 注册过的 db 集合（register_database / get_or_create 两入口插入）：
    // stop 收尾时逐个读 RunMetrics 的 du 终值落 DB_DU 事件（DBs 页磁盘占用）。
    CMUnorderedSet<CMString> registered_dbs_;
    // monitor.db 是否已开过（同进程多段 run 的 run_start_ms/run_restart_ms 区分）。
    bool monitor_db_opened_once_ = false;
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

    // 调度看门狗线程：周期检查 ready 任务是否长期得不到调度（调度卡死检测）。
    // 发现 stall 时输出 WARN（强制 flush）—— INFO/DEBUG 默认不 flush，
    // 卡死时进程被 SIGKILL 会丢缓冲日志，看门狗用 WARN 保证现场落盘。
    std::thread sched_watchdog_thread_;
    std::atomic<bool> sched_watchdog_running_{false};
    std::mutex sched_watchdog_mutex_;
    std::condition_variable sched_watchdog_cv_;
    // 上一次 watchdog 看到的 (ready_count, last_ready_task_id)，用于检测停滞。
    size_t sched_watchdog_last_ready_count_{0};
    uint64_t sched_watchdog_last_ready_id_{0};
    int sched_watchdog_stall_rounds_{0};

    // task 提交时的完整不变字段统一存储在 TaskMetadata.submission_ 里
    // （通过 metadata_->get_task(id)->submission_ 访问），不再需要单独维护
    // module/args/vars 的并行 map。单一来源消除了"两段式上锁拷贝"和字段
    // 同步遗漏（如本次 priority bug 的根源）。

    // Pre-fetched dependency locations: task_id → {object_name → (worker_id, host, port)}.
    // Updated on write_register, consumed on assign_task_to_worker（take 消费式读取）.
    struct CachedLocation {
        uint64_t worker_id = 0;
        CMString host;
        int32_t port = 0;
    };
    ConcurrentUnorderedMap<uint64_t, CMUnorderedMap<CMString, CachedLocation>> task_dependency_locations_;

    // db_instances_ 是 master 进程内 DB 路径的【唯一权威源】（收敛自原 db_registry_ 字符串副本）。
    // Database 对象内嵌 db_path_/data_path_（merge 后用 set_paths 更新），DbPathRequest/
    // IdxLoadAck/send_delete_data 均从此读路径，消除手动双写与 merge 后副本分叉。
    CMUnorderedMap<CMString, CMSharedPtr<Database>> db_instances_;
    // db_instances_ 容器锁：handler 并行（lane）与 Python 线程注册/merge 改路径
    // 并发访问容器本身；Database 内部状态由各自锁保护，锁外使用 shared_ptr 即安全。
    mutable std::shared_mutex db_instances_mutex_;
    // failed_tasks.bin append/读改写互斥（跨线程调用方见 persist_failed_task 注释）。
    std::mutex failed_tasks_file_mutex_;
    CMUnorderedSet<CMString> frozen_dbs_;
    // 非 stream 模式 pending frozen：db_path → task_id（待 task 完成确认）。
    // task 内 freeze 时登记 pending（拒其他 task 写，但不广播）；task 成功迁移到
    // frozen_dbs_ + 广播，task 失败/崩溃按 task_id 回滚清除（防永久死锁）。
    CMUnorderedMap<CMString, uint64_t> pending_frozen_dbs_;
    mutable std::mutex frozen_dbs_mutex_;
    static std::atomic<uint64_t> remote_task_counter_;

    // ── Merge task 跟踪（fly.merge_db）──────────────────────────────────
    // 每个 merge __merge_object task 的状态，由 on_merge_task_complete/Failed 更新，
    // wait_merge_tasks_complete 等待（erase_on_timeout=false：超时保留，条目
    // 生命周期跨 wait，由 cleanup_after_merge 统一消费+清理）。设计 §5.4：全部成功才删源。
    struct MergeTaskState {
        bool completed_ = false;
        bool success_ = false;
        CMString error_message_;
        CMVector<CMString> written_objects_;  // 成功时填入（full_name 列表）
        uint64_t worker_id_ = 0;  // 执行 merge task 的 worker（精确的对象持有者）
        CMString db_path_;        // 源 db_path（失败清理按 db 精确匹配，不误清并发 merge）
    };
    PendingRpcMap<uint64_t, MergeTaskState> merge_task_states_;

    // ── DeleteData ack 跟踪（merge 删源）──────────────────────────────
    // key = (db_path + ":" + worker_id) 的字符串，避免多 worker/db 并发删除时 ack 串台。
    // insert_if_absent 登记（Problem5 防重置）；wait 超时保留条目（erase_on_timeout=false，
    // 由 wait 末尾统一清理）；on_delete_data_ack 走持锁 complete。
    struct PendingDeleteData {
        bool completed_ = false;
        bool success_ = false;
        int32_t deleted_count_ = 0;
        CMString error_message_;
    };
    PendingRpcMap<CMString, PendingDeleteData> pending_delete_acks_;

    // ── MergeCleanup ack 跟踪（merge_db 返回前的全局一致性屏障）──
    // master 广播 MergeCleanup 后，必须等所有 worker 回 ack 才能重建自身 remote_idx +
    // 让 merge_db 返回。key = db_path（一次 merge_db 的 cleanup 是单 db 全员广播）。
    // 计数屏障（expected/received）；on_merge_cleanup_ack 持锁 complete（无条件
    // notify_all 替代原条件 notify，多出的空唤醒无害）。
    struct PendingMergeCleanup {
        uint64_t expected_count_ = 0;   // 期望的 ack 数（广播时的 worker 数）
        uint64_t received_count_ = 0;   // 已收到的 ack 数
    };
    PendingRpcMap<CMString, PendingMergeCleanup> pending_merge_cleanups_;

    // ── IdxLoad ack 跟踪（load_db 返回前的对象可见性屏障）────────────
    // load_db 对每个目标 worker 发 IdxLoadCommand 后必须等全部 Ack + master
    // rebuild_remote_idx 完成（remote_idx 更新与 mark_data_ready 落地）才可返回
    // ——此前是 time.sleep(1.0) 盲等，高负载下 worker idx 加载 2.2s 越过窗口，
    // load_db 返回后立即 read_object KeyError（100 轮压测实测）。remaining 经
    // idx_load_pending() 由 Python 轮询（等可见性标志，非盲 sleep）。
    // per-worker 集合精确跟踪（非总数计数）：重复 Ack 幂等 erase——总数计数
    // 下重放 Ack 会吃掉其他 worker 的份额，计数提前归 0 击穿可见性屏障
    //（单测 IdxLoadPendingVisibilityBarrier 抓获）。
    // remaining_：待完成 worker 数；-1 = 有 Ack 失败（worker 加载失败 /
    // unknown db_path），load_db 必须报错而非静默返回。
    struct PendingIdxLoad {
        int32_t remaining_ = 0;
        CMUnorderedSet<uint64_t> pending_workers_;
    };
    PendingRpcMap<CMString, PendingIdxLoad> pending_idx_loads_;

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

    // schedule_tasks 的 locality 预计算段（锁外执行，见 cpp 注释）。
    void compute_locality_hints(bool locality_on);
    // 统一「判死 → 持久化」收尾：依赖不可解 / 属性死锁两处同构
    //（组 error 由 make_error 回调产生）。
    void fail_and_persist_tasks(const CMVector<uint64_t>& task_ids,
                                const std::function<CMString(uint64_t)>& make_error);
    void assign_task_to_worker(uint64_t task_id, uint64_t worker_id);
    void update_dependency_location_cache(const CMString& object_name, uint64_t worker_id, const CMString& host, int32_t port);
    void heartbeat_check_loop();
    // 清理超时未注册的唤起占位符（worker_register_timeout>0 时生效；由
    // heartbeat_check_loop 周期调用，测试经 hook 直接驱动）。
    void check_expected_worker_timeouts(int64_t now);
    // 扫描断连宽限表：超时项 erase + handle_worker_death（判死路径统一入口）。
    // 由 heartbeat_check_loop 周期调用，测试经 hook 直接驱动。
    void check_grace_deadlines(int64_t now);
    // worker 正式判死（宽限超时 / drain 期断连 / 断连即死模式）：标 DEAD +
    // 恢复其 RUNNING task + rollback pending frozen + 存储接管/数据全灭快速失败。
    void handle_worker_death(uint64_t worker_id);
    // 判死联动收敛 pending RPC 期待（无限等待的安全性前提：等待只被显式失败
    // 信号终结，不被超时终结）：死亡 worker 的 IdxLoad 期待置 -1（load_db 侧
    // 显式报错）、DeleteData 期待 complete 失败（残留数据 WARN）、MergeTask
    // 期待 complete 失败（等价 on_merge_task_failed）、MergeCleanup 屏障视为
    // 该 worker 已清理（进程死亡 = 其内存索引天然不存在）。
    void settle_pending_for_dead_worker(uint64_t worker_id);
    // 数据全灭快速失败（可重入幂等）：W 持有对象中"全部 holder 均 DEAD"的，
    // 撤 ready + fail 等待调度的依赖 task。判死即时路径与接管超时兜底共用。
    void fail_orphan_data_objects(uint64_t worker_id);
    // 存储接管（storage_takeover_enabled）：找死 worker 同 host 存活
    // storage_only，按 recorded_workers_（_DB_META 内存镜像）加载该 host 全部
    // writer 的 idx（复用 IdxLoad 链路，worker 只读 restore_entries）。发起
    // 成功返回 true（延迟全灭 fail 至 deadline）；无 storage/无 writer/超上限
    // 返回 false（调用方走即时 fail）。
    bool try_storage_takeover(uint64_t worker_id);
    // 接管超时兜底：到期条目重跑 fail_orphan_data_objects（幂等重判——接管
    // 已完成则对象有活 holder，无 fail）并清除 pending。由
    // heartbeat_check_loop 周期调用。
    void check_takeover_deadlines(int64_t now);
    // 自动补齐（auto_storage_nodes_enabled，默认关）：周期检测「有活 worker
    // 但无活 storage_only」的 host，向该 host 任一活 hybrid worker 发
    // StorageSpawnRequest 让其本地 spawn storage worker（/proc/self/exe 同
    // 版本、SETSID 脱离进程树）。占位防重 + 失败退避（3 次放弃该 host）。
    // 由 heartbeat_check_loop 周期调用（auto_storage_check_interval 节流）。
    void check_storage_nodes(int64_t now);
    void on_storage_spawn_ack(uint64_t conn_id, const StorageSpawnAckMessage& msg);
    bool send_storage_spawn_to_worker(uint64_t worker_id, uint64_t spawn_worker_id);
    // 疑似重复注册的探测结论处理：ProbeAck → 拒绝挂起的后到者；注册重放
    // （旧连接断开触发）；deadline 兜底（heartbeat 循环周期检查）。
    void reject_deferred_register(uint64_t worker_id, const char* reason);
    void replay_deferred_register(uint64_t worker_id);
    void check_dup_register_deadlines(int64_t now);
    void attr_timeout_check_loop();
    void sched_watchdog_loop();

    // workers_mutex_ 临界区内只做快照/查表，reactor send 一律锁外（send 含 encode +
    // per-conn send mutex + conn_mutex_，锁内做会与 reactor 线程互拖放大临界区）。
    // 快照后连接断开的竞态由 transport 对未知 conn_id 的安全 -1 分支兜底
    //（同 on_master_remove/on_backup_request 既有快照模式）。
    CMVector<std::pair<uint64_t, uint64_t>> snapshot_worker_conns() const;  // (worker_id, conn_id)
    uint64_t lookup_worker_conn(uint64_t worker_id) const;                  // 0 = 未连接

    std::mutex schedule_mutex_;

    void on_worker_register(uint64_t conn_id, const RegisterMessage& msg);
    void on_disconnect(uint64_t conn_id);
    void on_error(uint64_t conn_id, int error_code);
    void on_data_query_dispatch(uint64_t conn_id, const DataQueryMessage& msg);
    void on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg);
    void record_worker_info(const CMString& object_name, const CMString& db_path,
                            uint64_t worker_id, const CMString& writer_id);
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
    // 选 backup 目标 worker：避开对象所有现有副本的 host（host 级分散）；
    // host 全冲突时 best-effort 回退到「无副本」的 worker。返回 0 = 无可用目标。
    uint64_t select_backup_worker(const CMString& object_name);

    void trigger_auto_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_path);

    // worker → master：聚合 worker 上报的 TIER2 读增量（EWMA 衰减），并据此判定 backup。
    void on_worker_backup_suggest(uint64_t conn_id, const WorkerBackupSuggestMessage& msg);
    void evaluate_and_maybe_backup(const CMString& object_name);

    void persist_failed_task(const FailedTaskRecord& record);
    void remove_persisted_task(uint64_t task_id);
    // 失败记录落点：归属 db 非空 → {owner_db_path}/failed_tasks.bin（task 归属
    // 规则，元信息随 db 目录自包含）；无归属 task → {log_dir}/failed_tasks.bin。
    CMString get_failed_tasks_file_path(const CMString& owner_db_path) const;

    void on_master_freeze(const CMString& db_path);
    void on_master_remove(const CMString& db_path, const CMString& object_name);  // master 进程内 remove（清 provenance + 通知 worker）
    std::pair<CMString, TaskErrorType> on_master_register_write(const CMString& db_path, const CMString& name, int64_t compressed_size);


    // worker 的 hostname/ip 已收编进 WorkerManager::WorkerInfo（受其 mutex_ 保护），
    // 不再单独维护并行 map——消除原 worker_to_hostname_/worker_to_ip_ 的无锁数据竞争。

    // 已写入 db meta 的 worker 元组去重：insert 返回是否新插入，
    // 副作用（append_worker_info_to_meta）据此在锁外恰好执行一次。
    ConcurrentUnorderedSet<std::tuple<CMString, CMString, CMString>> recorded_workers_;

    // ObjectBackupScore 定义已前移至 on_merge_task_failed 之后（test hook 需在
    // FLY_ENABLE_TEST_HOOKS 区引用其完整类型）。
    ConcurrentUnorderedMap<CMString, ObjectBackupScore> backup_scores_;

    // 按 db 分组：outer key = db_path，inner key = short_name，value = write_context_hash。
    // 嵌套结构让 freeze 时 cleanup_provenance_for_db 一次 erase 整个 db，无需前缀扫描。
    CMUnorderedMap<CMString, CMUnorderedMap<CMString, CMString>> write_provenance_;
    mutable std::mutex provenance_mutex_;

    // provenance 嵌套访问封装（内部均持 provenance_mutex_）。
    // 校验并登记：首次或 hash 一致返回 true；hash 冲突返回 false 并填 err_msg。
    bool provenance_check_and_register(const CMString& db_path, const CMString& short_name,
                                       const CMString& hash, CMString& err_msg);
    void provenance_erase(const CMString& db_path, const CMString& short_name);   // erase inner；空则清 outer
    void cleanup_provenance_for_db(const CMString& db_path);   // freeze 用：整体 erase outer
    CMString provenance_lookup(const CMString& db_path, const CMString& short_name);  // backup 继承用


    void do_drain_and_stop();
    void persist_pending_tasks();
    // 从 TaskMetadata 构造 FailedTaskRecord（统一入口，消除 4 处手动复制）。
    // error_msg 覆盖 record 的错误信息（失败原因由调用方提供，metadata 里
    // 的 error_message_ 此时可能尚未设置或语义不同）。
    FailedTaskRecord make_failed_record(uint64_t task_id, const CMString& error_msg);
    void notify_drain_if_active();
};

}  // namespace fly
