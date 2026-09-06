#include <agent/cpp/master_agent.h>
#include <agent/cpp/graceful_shutdown.h>
#include <log/cpp/logger.h>
#include <message/cpp/message_macros.h>
#include <message/cpp/message_registry.h>
#include <core/cpp/config.h>
#include <core/cpp/system_info.h>
#include <sstream>
#include <core/cpp/process_info.h>
#include <core/cpp/graceful_exit.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/object_cache.h>
#include <storage/cpp/network_chunk_source.h>
#include <storage/cpp/memory_chunk_source.h>
#include <common/runtime/cpp/write_context_hash.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fly {

std::atomic<uint64_t> MasterAgent::remote_task_counter_{100000};

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), listen_port_(port), running_(false),
      graph_(CMMakeUnique<DependencyGraph>()),
      worker_manager_(CMMakeUnique<WorkerManager>()) {
}

MasterAgent::~MasterAgent() {
    stop();
}

void MasterAgent::start() {
    if (running_) return;

    draining_ = false;
    shutdown_requested_ = false;

    graph_ = CMMakeUnique<DependencyGraph>();
    worker_manager_ = CMMakeUnique<WorkerManager>();

    INFO("MasterAgent start() called, listening on {}:{}", host_, port_);

    auto transport = create_connection_manager("tcp");
    // 用构造时的请求端口 bind：固定端口用户意图被尊重；port 0 每次拿全新临时端口，
    // 避免重启复用旧端口在 close→rebind 窗口内被并发进程抢占（EADDRINUSE）。
    if (!transport || !transport->listen(host_, listen_port_)) {
        // master 监听失败 = 不可服务：干净退出 start()（running_ 保持 false），
        // 调用方经 is_running() 观察（与 worker 的 listen/connect 失败路径对称）。
        ERR("Master transport listen failed on {}:{} — master cannot start",
            host_, listen_port_);
        return;
    }

    // handler 并行 lane：同连接消息严格保序（Register→*、WriteRegister→TaskComplete
    // 等协议顺序依赖），跨连接并行。schedule_tasks 等重 handler 不再阻塞 reactor 线程。
    size_t handler_lanes = static_cast<size_t>(Config::instance()->get_int("handler_lanes"));
    reactor_ = CMMakeUnique<Reactor>(std::move(transport), handler_lanes);

    // 顺序敏感域（P3-26 家族架构收口）：worker 身份生命周期协议依赖跨连接的
    // 处理顺序——REGISTER（含重连注册/dup 判定/deferred 重放）、WorkerProbeAck
    // （活性确认）与断连事件统一走保留串行 lane，跨连接 FIFO，消除跨 lane
    // check-act 交错窗口。HEARTBEAT 等收敛型消息不入域（高频且顺序无关）。
    // WORKER_EXIT 也入域：它是连接的最后一条消息，必须先于同连接的 DISCONNECT
    // 事件处理（否则 on_disconnect 归类时读不到 exit_confirmed 标记——与断连
    // 事件分属消息 lane/串行 lane 会并行竞争）。
    // 后续新增「必须全局串行」的消息一律加入此域（见 Reactor::set_serialized_domain）。
    reactor_->set_serialized_domain(
        {MessageType::REGISTER, MessageType::WORKER_PROBE_ACK, MessageType::WORKER_EXIT},
        /*lifecycle_events=*/true);

    port_ = static_cast<uint16_t>(reactor_->get_bound_port());

    // master 进程：初始化 MessageSink（打开 message.log）+ 把 MSG 宏的 push 绑定为
    // 本进程直写（master 自身 message 直接进 message.log + terminal，不走网络）。
    MessageSink::instance()->init(Config::instance()->get_str("log_dir"));
    fly::set_message_push_func([](fly::LogLevel level, const fly::CMString& domain_id, int32_t source, const fly::CMString& msg) {
        MessageSink::instance()->handle_local(level, domain_id, source, msg);
    });
    // system sink（FLY::0000 等）：master 绑定为 MessageSink，使系统 message 进 message.log + terminal。
    // 豁免 master 打印配额（honor_quota=false）—— FLY::0000 是基础信息，必须输出。
    fly::set_system_sink_func([](fly::LogLevel level, const fly::CMString& domain_id, int32_t source, const fly::CMString& msg) {
        MessageSink::instance()->handle_local(level, domain_id, source, msg, /*honor_quota=*/false);
    });

    // 注册 master 侧用到的流程性 message id（白名单 + 级别绑定）。
    // STOR::0001: 数据库 freeze 完成；TASK::0001: task 不可恢复失败；
    // AGENT::0001: worker 注册；AGENT::0002: worker 断开；FLY::0001: master drain 完成。
    fly::MessageRegistry::instance().register_id("STOR::0001", fly::LogLevel::INFO);
    fly::MessageRegistry::instance().register_id("TASK::0001", fly::LogLevel::ERROR);
    fly::MessageRegistry::instance().register_id("AGENT::0001", fly::LogLevel::INFO);
    fly::MessageRegistry::instance().register_id("AGENT::0002", fly::LogLevel::WARN);
    // AGENT::0003: worker 判死导致数据全灭（依赖 task 已快速失败，ERROR 级提醒用户）。
    fly::MessageRegistry::instance().register_id("AGENT::0003", fly::LogLevel::ERROR);
    // AGENT::0004: 判死后同 host storage 接管读服务（INFO 级集群自愈里程碑）。
    fly::MessageRegistry::instance().register_id("AGENT::0004", fly::LogLevel::INFO);
    // AGENT::0005: master 自动补齐存储节点（INFO 级拓扑变化提醒）。
    fly::MessageRegistry::instance().register_id("AGENT::0005", fly::LogLevel::INFO);
    // AGENT::0006: 宽限耗尽 worker 未重连（WARN 级提醒 + 手动重启命令；worker
    // 侧重连上限与之对等，两侧同宽限收敛）。
    fly::MessageRegistry::instance().register_id("AGENT::0006", fly::LogLevel::WARN);
    fly::MessageRegistry::instance().register_id("FLY::0001", fly::LogLevel::INFO);
    // FLY::0002: run 结束里程碑（总耗时 + summary 文件地址，stop_impl 尾部）。
    fly::MessageRegistry::instance().register_id("FLY::0002", fly::LogLevel::INFO);

    // 绑定配额变更回调：用户 set_*_limit 后触发，把当前配额广播给所有在线 worker
    // （支持运行时动态修改）。worker 上线时 on_worker_register 也会补发一次。
    fly::set_limit_change_callback([this]() {
        broadcast_message_limits();
    });

    reactor_->register_handler<RegisterMessage>(
        [this](uint64_t conn_id, const RegisterMessage& msg) {
            on_worker_register(conn_id, msg);
        });

    reactor_->register_handler<HeartbeatMessage>(
        [this](uint64_t conn_id, const HeartbeatMessage& msg) {
            on_heartbeat(conn_id, msg);
        });

    // worker 正常退出声明（graceful 分支关连接前发出）：仅登记归类，清理
    // 仍等断连事件——不依赖本消息的到达时序；收不到（崩溃/网络断）时
    // on_disconnect 靠 shutdown_pending 指令标记兜底或走异常判死。
    reactor_->register_handler<WorkerExitMessage>(
        [this](uint64_t conn_id, const WorkerExitMessage& msg) {
            (void)conn_id;
            exit_confirmed_workers_.insert(msg.worker_id_);
            DBG("worker {} declared graceful exit (reason={})",
                msg.worker_id_, static_cast<int>(msg.exit_reason_));
        });

    reactor_->register_handler<MonitorSampleMessage>(
        [this](uint64_t conn_id, const MonitorSampleMessage& msg) {
            on_monitor_sample(msg);
        });

    reactor_->register_handler<MonitorTaskIoMessage>(
        [this](uint64_t conn_id, const MonitorTaskIoMessage& msg) {
            on_task_io_report(msg);
        });

    reactor_->register_handler<TaskCompleteMessage>(
        [this](uint64_t conn_id, const TaskCompleteMessage& msg) {
            on_task_complete(conn_id, msg);
        });

    reactor_->register_handler<TaskFailedMessage>(
        [this](uint64_t conn_id, const TaskFailedMessage& msg) {
            on_task_failed(conn_id, msg);
        });

    reactor_->register_handler<TaskSubmitMessage>(
        [this](uint64_t conn_id, const TaskSubmitMessage& msg) {
            INFO("TaskSubmit received: task_name={}, module={}", msg.task_name_, msg.task_module_);
            uint64_t task_id = ++remote_task_counter_;
            TaskSubmissionSpec spec;
            spec.name_ = msg.task_name_;
            spec.module_ = msg.task_module_;
            spec.args_ = msg.args_;
            spec.inputs_ = msg.inputs_;
            spec.required_capabilities_ = msg.required_capabilities_;
            spec.attribute_timeout_ = msg.attribute_timeout_;
            spec.write_context_hash_ = msg.write_context_hash_;
            spec.vars_ = msg.vars_;
            spec.owner_db_path_ = msg.owner_db_path_;
            spec.priority_ = msg.priority_;
            submit_task(task_id, spec);
            // Ack 强语义：request_id 非 0（worker 转发的同步提交）必须回执，
            // 带回分配的 task_id；master 本地提交不设 request_id，无 Ack。
            if (msg.request_id_ != 0) {
                TaskSubmitAckMessage ack;
                ack.request_id_ = msg.request_id_;
                ack.task_id_ = task_id;
                ack.accepted_ = true;
                reactor_->send(conn_id, ack);
            }
        });

    reactor_->register_handler<DbPathRequestMessage>(
        [this](uint64_t conn_id, const DbPathRequestMessage& msg) {
            INFO("DbPathRequest received: db_path={}", msg.db_path_);

            DbPathResponseMessage response;
            response.db_path_ = msg.db_path_;

            // 路径权威源收敛到 db_instances_（Database 内嵌 db_path_/data_path_）。
            {
                // 锁内只取路径值拷贝；send 移出容器锁（D2 拆除锁内网络 IO）。
                std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
                auto it = db_instances_.find(msg.db_path_);
                if (it != db_instances_.end()) {
                    response.db_path_ = it->second->get_db_path();
                    response.data_path_ = it->second->get_data_path();
                    response.success_ = true;
                } else {
                    // 失败也必须原样带回 db_path_：worker 的 on_db_path_response
                    // 以 msg.db_path_ 为 key 匹配 pending 请求（原实现清空该字段
                    // → 空 key 永不匹配 → 请求方稳定等满 5s 超时，
                    // OnDbPathResponseFailure 实测 5159ms）。
                    response.db_path_ = msg.db_path_;
                    response.data_path_ = "";
                    response.success_ = false;
                }
            }

            reactor_->send(conn_id, response);
        });

    reactor_->register_handler<DataQueryMessage>(
        [this](uint64_t conn_id, const DataQueryMessage& msg) {
            on_data_query_dispatch(conn_id, msg);
        });

    reactor_->register_handler<WriteRegisterMessage>(
        [this](uint64_t conn_id, const WriteRegisterMessage& msg) {
            on_write_register(conn_id, msg);
        });

    reactor_->register_handler<WorkerPropertyUpdateMessage>(
        [this](uint64_t conn_id, const WorkerPropertyUpdateMessage& msg) {
            on_worker_property_update(conn_id, msg);
        });

    reactor_->register_handler<DatabaseFreezeNotification>(
        [this](uint64_t conn_id, const DatabaseFreezeNotification& msg) {
            on_database_freeze_request(conn_id, msg);
        });

    reactor_->register_handler<RemoveRequestMessage>(
        [this](uint64_t conn_id, const RemoveRequestMessage& msg) {
            on_remove_request(conn_id, msg);
        });

    reactor_->register_handler<BackupRequestMessage>(
        [this](uint64_t conn_id, const BackupRequestMessage& msg) {
            on_backup_request(conn_id, msg);
        });

    reactor_->register_handler<WorkerBackupSuggestMessage>(
        [this](uint64_t conn_id, const WorkerBackupSuggestMessage& msg) {
            on_worker_backup_suggest(conn_id, msg);
        });

    reactor_->register_handler<IdxLoadAckMessage>(
        [this](uint64_t conn_id, const IdxLoadAckMessage& msg) {
            on_idx_load_ack(conn_id, msg);
        });

    reactor_->register_handler<StorageSpawnAckMessage>(
        [this](uint64_t conn_id, const StorageSpawnAckMessage& msg) {
            on_storage_spawn_ack(conn_id, msg);
        });

    reactor_->register_handler<WorkerProbeAckMessage>(
        [this](uint64_t conn_id, const WorkerProbeAckMessage& msg) {
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            dup_confirmed_alive_.update(msg.worker_id_, [&](int64_t& t) { t = now; });
            // 旧实例活着 → 挂起的后到者拒绝。
            reject_deferred_register(msg.worker_id_, "probe confirmed existing instance alive");
            DBG("Worker {} confirmed alive via probe (conn {})", msg.worker_id_, conn_id);
        });

    reactor_->register_handler<DeleteDataAckMessage>(
        [this](uint64_t conn_id, const DeleteDataAckMessage& msg) {
            on_delete_data_ack(conn_id, msg);
        });

    reactor_->register_handler<MergeCleanupAckMessage>(
        [this](uint64_t conn_id, const MergeCleanupAckMessage& msg) {
            on_merge_cleanup_ack(conn_id, msg);
        });

    reactor_->register_handler<LogMessage>(
        [this](uint64_t conn_id, const LogMessage& msg) {
            on_log_message(conn_id, msg);
        });

    reactor_->register_handler<MessageCountReportMessage>(
        [this](uint64_t conn_id, const MessageCountReportMessage& msg) {
            on_message_count_report(conn_id, msg);
        });

    reactor_->register_handler<VarSetMessage>(
        [this](uint64_t conn_id, const VarSetMessage& msg) {
            on_var_set(conn_id, msg);
        });

    reactor_->register_handler<VarGetMessage>(
        [this](uint64_t conn_id, const VarGetMessage& msg) {
            on_var_get(conn_id, msg);
        });

    reactor_->register_handler<VarRemoveMessage>(
        [this](uint64_t conn_id, const VarRemoveMessage& msg) {
            on_var_remove(conn_id, msg);
        });

    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });

    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });

    scheduler_ = CMMakeUnique<TaskScheduler>(graph_.get(), worker_manager_.get());
    scheduler_->set_locality_preference(Config::instance()->get_int("locality_scheduling_enabled") == 1);
    metadata_ = CMMakeUnique<TaskManager>();

    heartbeat_monitor_ = CMMakeUnique<HeartbeatMonitor>(
        worker_manager_.get(), Config::instance()->get_int("heartbeat_timeout"));

    // RunMetricsCollector：master 骨架采样（tick 线程）；worker monitor 通道
    // 成组样本（on_monitor_sample）按真实 epoch 时刻最近邻合并（合成推迟到
    // build_summary）。tick 线程在 do_drain_and_stop 停止，summary 在
    // stop_impl 尾部输出。
    run_metrics_ = CMMakeUnique<RunMetricsCollector>();
    run_metrics_->start(Config::instance()->get_int("metrics_tick_seconds"));

    // cluster monitor 落盘层：{log_dir}/monitor.db 单写（open 失败仅告警，
    // 本 run 降级为无持久化监控，不影响主流程）。同进程多段 run（stop 后
    // 再 start）：上一段已 close，此处重开续写（表结构 IF NOT EXISTS 共存，
    // 首段写 run_start_ms、后续段写 run_restart_ms，事件流连续）。
    metrics_db_ = CMMakeUnique<MetricsDb>();
    if (Config::instance()->get_int("monitor_db_enabled")) {
        if (metrics_db_->open(Config::instance()->get_str("log_dir"))) {
            metrics_db_->record_run_meta(
                monitor_db_opened_once_ ? "run_restart_ms" : "run_start_ms",
                std::to_string(monitor_epoch_ms_now()));
            metrics_db_->record_run_meta("hostname", ProcessInfo::instance()->hostname());
            monitor_db_opened_once_ = true;
            self_sample_gap_ms_ =
                Config::instance()->get_int("monitor_exec_sample_interval_ms");
            if (self_sample_gap_ms_ <= 0) self_sample_gap_ms_ = 200;
            // master 自身也登记进 workers 表（wid=0，role=master）：monitor_
            // self 循环写的 wid=0 样本在 GUI Workers 页有归属（显示为
            // master，非 worker 0——用户裁定；无注册/退出生命周期）。
            metrics_db_->record_worker_registered(
                0, ProcessInfo::instance()->hostname(), "", "master", "");
            monitor_self_running_ = true;
            monitor_self_thread_ = std::thread([this] { monitor_self_loop(); });
        } else {
            WARN("monitor.db open failed — monitor persistence disabled for this run");
        }
    }

    heartbeat_check_running_ = true;
    heartbeat_check_thread_ = std::thread([this] { heartbeat_check_loop(); });

    attr_timeout_check_running_ = true;
    attr_timeout_check_thread_ = std::thread([this] { attr_timeout_check_loop(); });

    sched_watchdog_running_ = true;
    sched_watchdog_thread_ = std::thread([this] { sched_watchdog_loop(); });

    reactor_thread_ = std::thread([this] {
        reactor_->run();
        // run() 退出后先等 lane 排空再 reset：reset 会先置空 reactor_ 成员再跑
        // 析构（含池 join），迟到的 handler 经本对象 reactor_ 访问会解引用空指针。
        reactor_->drain_handlers();
        DataService::instance()->stop_data_server();
        reactor_.reset();
    });
    reactor_->wait_until_running();
    running_ = true;
    // 同进程多段 run（stop 后再 start）：复位 stop 防重入标志，让本段的
    // stop 走完整收尾（drain + metrics_db close 等）。
    draining_.store(false);

    auto dsInst = DataService::instance();
    int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
    dsInst->start_data_server(host_, 0, data_server_threads);
    data_server_port_ = static_cast<int32_t>(dsInst->get_data_port());
    DataService::instance()->register_worker(0, host_, data_server_port_);

    // 流式读接线（恒流式改造 2026-08-30）：master 与 worker 同款 streaming
    // handler——常规读统一流式传输（#5 裁定"始终不使用整体接收"彻底落地，
    // master 原整缓冲主路径退役；快路径整帧由 chunked_=false 分支包内存源）。
    dsInst->set_streaming_read_handler(
        [this](const CMString& host, int32_t port,
               const CMString& name) -> std::tuple<bool, CMSharedPtr<fly::ChunkSource>, uint64_t, ReadError> {
            auto ex = data_client_pool_.request_raw_exchange(host, port, name);
            if (!ex.success) {
                return {false, nullptr, 0, ex.rerr};
            }
            if (!ex.meta.chunked_) {
                if (!ex.whole_data || ex.whole_data->empty()) {
                    data_client_pool_.release_borrowed_fd(ex.handle->get(), true);
                    return {false, nullptr, 0, ReadError::NETWORK};
                }
                auto mem = CMMakeShared<fly::SharedMemoryChunkSource>(
                    ex.whole_data->data(), ex.whole_data->size(), ex.whole_data);
                mem->is_temp = ex.meta.is_temp_;
                if (mem->failed()) {
                    return {false, nullptr, 0, ReadError::CHECKSUM};
                }
                return {true, mem, mem->block_area_len(), ReadError::NONE};
            }
            int64_t chunks = Config::instance()->get_int("stream_buffer_chunks");
            uint64_t queue_limit = static_cast<uint64_t>(chunks > 0 ? chunks : 16) *
                                   ex.meta.chunk_frame_bytes_;
            auto src = CMMakeShared<fly::NetworkChunkSource>(
                data_client_pool_.transport(), ex.handle, ex.meta,
                [pool = &data_client_pool_, fd = ex.handle->get()](bool healthy) {
                    pool->release_borrowed_fd(fd, healthy);
                },
                queue_limit);
            src->is_temp = ex.meta.is_temp_;
            src->start();
            return {true, src,
                    ex.meta.total_compressed_len_ > ex.meta.trailer_len_
                        ? ex.meta.total_compressed_len_ - ex.meta.trailer_len_ : 0,
                    ReadError::NONE};
        });

    // Master is the location authority: its remote_idx already holds every
    // replica, so read_object goes TIER1 → TIER2 only (no TIER3 — querying
    // itself would be a self-referential no-op and historically raced with
    // WriteRegister on a different reactor fd). Register a direct handler so
    // TIER2 can fetch from the owning worker's DataServer via the pool,
    // mirroring the worker side.
    dsInst->set_direct_compressed_read_handler(
        [this](const CMString& host, int32_t port,
               const CMString& name) -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
            uint64_t rid = static_cast<uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())) ^
                           static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            auto [success, data, py_name, hash, error, rerr] =
                data_client_pool_.request(host, port, name, 0, rid);
            if (!success) {
                ERR("master direct read failed for {}: {}", name, error);
                return {false, nullptr, {}, {}, rerr};
            }
            return {true, data, std::move(py_name), std::move(hash), ReadError::NONE};
        });

    // Master's TIER3 is a pure LOCAL lookup (no network DataQuery to self —
    // that historically raced with WriteRegister on a different reactor fd).
    // remote_idx is authoritative here, so "query master for location" reduces
    // to consulting it directly. This handler exists so that, when TIER2 has no
    // replica yet, read_raw_compressed still returns a meaningful
    // can_still_produce (has_pending||has_running) — which wait_obj relies on to
    // distinguish "object may still be produced" from "truly missing".
    dsInst->set_remote_compressed_read_handler([this](const CMString& name) -> std::tuple<bool, bool> {
        auto ds = DataService::instance();
        // If remote_idx already lists replicas, signal "refreshed" so TIER2
        // retries them; otherwise compute can_still_produce locally.
        if (ds->has_remote_location(name)) {
            return {true, false};
        }
        bool has_pending = !graph_->get_pending_tasks().empty();
        bool has_running = metadata_->has_tasks_with_status(TaskStatus::RUNNING);
        return {false, has_pending || has_running};
    });


    INFO("MasterAgent started, reactor thread running");

    // FLY::0000：打印启动基础信息（豁免配额，master 进 system.log + terminal）。
    // system_info 多行文本逐行经 emit_system_message 输出，source=0 表启动信息。
    {
        CMString info = SystemInfo::format_startup_info("master", port_);
        std::istringstream iss(info);
        CMString line;
        while (std::getline(iss, line)) {
            if (!line.empty()) {
                fly::emit_system_message(fly::LogLevel::INFO, "FLY::0000", 0, line);
            }
        }
    }

    register_shutdown_callback([this]() {
        // graceful_exit（致命错误，如 write-back 落盘失败）→ 快速退出：
        // 数据完整性已破坏，等待 task/WBQ 无意义（磁盘满时 persist 也会失败），
        // 只会拖长死亡。fail 善后由 fast_exit 统一处理。
        this->fast_exit("graceful_exit (fatal error)");
        fly::Logger::shutdown();
    });
}

void MasterAgent::stop() {
    stop_impl(false, "");
}

void MasterAgent::fast_exit(const CMString& reason) {
    stop_impl(true, reason);
}

void MasterAgent::stop_impl(bool fast, const CMString& reason) {
    if (draining_.exchange(true)) {
        // 已有 stop 在执行：fast 请求转打断标志，让正在优雅等待的 drain
        // 提前结束并转入快速路径（fail 善后 + StopNow 广播）。
        if (fast) {
            fast_exit_requested_.store(true);
            // 持锁 notify（规范）：drain waiter 的条件检查在锁内，防 lost wakeup。
            std::lock_guard<std::mutex> lk(drain_mutex_);
            drain_cv_.notify_all();
        }
        return;
    }
    if (!running_) {
        do_drain_and_stop();
        return;
    }

    // 高精度时间戳，定位 stop() 各阶段耗时（用于排查 runqa 偶发超时）。
    auto _t0 = std::chrono::steady_clock::now();
    auto _elapsed = [&]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - _t0)
            .count();
    };

    if (fast) {
        WARN("MasterAgent fast_exit: {} — skipping drain, failing tasks, stopping workers",
             reason.empty() ? CMString("unspecified") : reason);
        if (metrics_db_) {
            metrics_db_->record_event("run", "FAST_EXIT", 0, 0,
                                      reason.empty() ? CMString("unspecified") : reason);
        }
    } else {
        INFO("MasterAgent stop() called, entering drain phase");
        if (metrics_db_) metrics_db_->record_event("run", "DRAIN_START");
    }

    // Phase 1（仅正常收尾）: 等 RUNNING task 全部完成。
    // 兜底层级：①fast_exit 打断（SIGTERM/致命错误转快速路径）②心跳判死链
    //（worker 全灭 → RUNNING fail → 条件达成）③断连宽限超时 ④drain_timeout
    // 长上限（默认 600s，Config 可调，0=无限逃生口）——覆盖"worker 活着但
    // complete 丢失"的僵死路径（4 实例压测实测：连接正常、心跳正常、
    // RUNNING 永不归零，无限等待卡死 300s）。超时与 30s 硬超时的本质区别：
    // 超时后转入 fast 路径做 fail 善后（failed record 留痕）+ StopNow 收尾，
    // 而非旧版的无善后裸奔。
    if (!fast) {
        int64_t drain_timeout_s = Config::instance()->get_int("drain_timeout_seconds");
        std::unique_lock<std::mutex> lock(drain_mutex_);
        while (true) {
            if (fast_exit_requested_.load()) {
                WARN("Drain interrupted by fast_exit request (elapsed={}ms) — switching to fast path",
                     _elapsed());
                fast = true;
                break;
            }
            int running_count = metadata_->count_tasks_by_status(TaskStatus::RUNNING);
            if (running_count == 0) break;
            // [SD] stderr 通道：压测取证发现 Logger 实例偶发吞 INFO（EndToEnd
            // 段 Drain/Executing 全不可见而 stderr 正常）——drain 等待对象走
            // stderr 保证可见。
            fprintf(stderr, "[SD] drain waiting: this=%p running=%d elapsed=%lldms\n",
                    static_cast<const void*>(this), running_count,
                    static_cast<long long>(_elapsed()));
            INFO("Drain: waiting for {} running tasks to complete (elapsed={}ms)", running_count, _elapsed());
            if (drain_timeout_s <= 0) {
                drain_cv_.wait(lock);
            } else {
                if (drain_cv_.wait_for(lock, std::chrono::seconds(drain_timeout_s)) ==
                        std::cv_status::timeout) {
                    WARN("Drain timeout ({}s) with {} tasks still running (elapsed={}ms) — "
                         "switching to fast path (fail + persist)",
                         drain_timeout_s, running_count, _elapsed());
                    fast = true;
                    break;
                }
            }
        }
    }

    if (fast) {
        // 快速路径 fail 善后：全部 RUNNING task 判失败并持久化 failed record
        //（轻量版 on_task_failed：状态一致性 + 留痕；dirty_objects 清理/下游级联
        // 无意义——worker 即将被 StopNow 终止，没有下游会再跑）。
        // PENDING 的 failed record 化由尾部 persist_pending_tasks 统一处理。
        auto running_ids = metadata_->get_task_ids_by_status(TaskStatus::RUNNING);
        for (uint64_t tid : running_ids) {
            FailedTaskRecord record = make_failed_record(
                tid, "Master fast exit: task aborted (reason=" +
                         (reason.empty() ? CMString("unspecified") : reason) + ")");
            {
                std::lock_guard<std::mutex> lk(schedule_mutex_);
                metadata_->fail_task(tid, "Master fast exit: task aborted");
                graph_->remove_task(tid);
            }
            // graceful_exit 通道磁盘可能已坏（落盘失败才触发退出）：persist 失败
            // 记 WARN 不阻塞——状态一致性（fail_task）优先于持久化留痕。
            try {
                persist_failed_task(record);
            } catch (const std::exception& e) {
                WARN("Fast exit: persist task {} failed ({}) — continuing", tid, e.what());
            } catch (...) {
                WARN("Fast exit: persist task {} failed (unknown) — continuing", tid);
            }
            WARN("Fast exit: task_id={} failed and persisted", tid);
        }
    } else {
        INFO("Drain: all tasks completed, shutting down workers (elapsed={}ms)", _elapsed());
        if (metrics_db_) metrics_db_->record_event("run", "DRAIN_DONE");
    }

    // Message summary：诊断性输出。正常路径保留（worker 活着，上报快）；
    // 快速路径跳过——不挡退出。
    if (!fast) {
        collect_and_print_message_summary();
        INFO("Message summary done (elapsed={}ms)", _elapsed());
    }

    // Phase 2: 停止 worker。fast → StopNow（worker kill 自身，亚秒断连）；
    // 正常 → Shutdown（worker 优雅退：flush coverage + WBQ drain）。
    // monitor 落盘：关停指令事件先于后续 DEAD——GUI 据此区分「正常退出」
    // （EXITED）与「异常死亡」（心跳超时/宽限耗尽判死，无指令先行）。
    for (const auto& [worker_id, conn_id] : snapshot_worker_conns()) {
        if (metrics_db_) {
            metrics_db_->record_worker_event(worker_id, fast ? "STOP_NOW_SENT" : "SHUTDOWN_SENT");
        }
        // 发送前登记「主动关停」标记：worker 的断连据此归类为正常退出
        //（handle_worker_exit），不进判死链。insert 先于 send——send 后
        // worker 立即断连的竞态窗口由先登记封闭；send 前恰好崩溃的极小
        // 窗口也归 exit（master 正要停它，语义可接受）。
        shutdown_pending_workers_.insert(worker_id);
        if (fast) {
            INFO("Sending STOP_NOW to worker_id={} (elapsed={}ms)", worker_id, _elapsed());
            StopNowMessage msg;
            msg.reason_ = reason;
            reactor_->send(conn_id, msg);
        } else {
            INFO("Sending shutdown to worker_id={} (elapsed={}ms)", worker_id, _elapsed());
            reactor_->send(conn_id, ShutdownMessage{});
        }
    }

    // Phase 3: 等 worker 断连。fast 用短宽限（StopNow 自杀 → OS 关 fd，亚秒；
    // 2s 仅兜进程僵死）；正常路径 worker 优雅退含 WBQ drain + coverage flush，
    // 保留 10s 上界。死连接（对端已崩溃/FIN 未处理、fake conn）不等——
    // 表清理仍由 on_disconnect 负责，此处只是不等它。
    {
        std::unique_lock<std::mutex> lock(workers_mutex_);
        auto deadline = std::chrono::steady_clock::now() +
                        (fast ? std::chrono::seconds(2) : std::chrono::seconds(10));
        while (true) {
            bool any_alive = false;
            for (const auto& [wid, cid] : worker_to_conn_) {
                (void)wid;
                if (reactor_ && reactor_->is_connected(cid)) {
                    any_alive = true;
                    break;
                }
            }
            if (!any_alive) break;
            if (workers_drained_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                WARN("{}: {} workers still connected after wait (elapsed={}ms)",
                     fast ? "STOP_NOW" : "Shutdown",
                     worker_to_conn_.size(), _elapsed());
                break;
            }
        }
    }

    INFO("MasterAgent stop(): workers disconnected, persisting pending (elapsed={}ms)", _elapsed());
    persist_pending_tasks();
    do_drain_and_stop();
    // RunSummary（用户裁定输出形态）：内容直写 {log_dir}/runtime.summary 与
    // {log_dir}/db.summary（独立 ofstream，不经 Logger——退出期 Logger INFO 有
    // 偶发吞行前科）；用户日志只打文件地址与总耗时，不直接展示 summary。
    if (run_metrics_) {
        auto [rt_path, db_sum_path] = run_metrics_->write_summary_files(
            Config::instance()->get_str("log_dir"),
            Config::instance()->get_int("metrics_tick_seconds"));
        const double dur_s = run_metrics_->duration_seconds();
        MSG("FLY::0002", 1, "run finished in {:.1f}s — runtime summary: {} | db summary: {}",
            dur_s, rt_path, db_sum_path);
        INFO("RunSummary files written: runtime={} db={} duration={:.1f}s",
             rt_path, db_sum_path, dur_s);
        // DBs 页磁盘占用收尾：summary 补测完成后（frozen 终值 + active db
        // 退出 du 均已就绪），对全部注册 db 落一次 DB_DU 终值。
        if (metrics_db_) {
            for (const auto& db_path : registered_dbs_) record_db_du(db_path);
        }
    }
    // monitor.db 收尾：停写线程并同步 flush 全部余量后关闭（早于 Logger 关闭
    // 与静态析构——退出期时序，P3-18 家族约束）。此后文件为干净终态，
    // GUI 可随时只读打开。
    if (monitor_self_running_.exchange(false)) {
        {
            std::lock_guard<std::mutex> lk(monitor_self_mutex_);
            monitor_self_cv_.notify_all();  // 持锁 notify
        }
        if (monitor_self_thread_.joinable()) monitor_self_thread_.join();
    }
    if (metrics_db_) {
        metrics_db_->record_run_meta("run_end_ms", std::to_string(monitor_epoch_ms_now()));
        metrics_db_->close();
    }
    INFO("MasterAgent stop(): fully stopped (elapsed={}ms)", _elapsed());
}

void MasterAgent::do_drain_and_stop() {
    INFO("MasterAgent performing full cleanup");
    // 流程 message：master drain 完成（与 FLY::0000 启动信息对称的关闭里程碑）。
    MSG("FLY::0001", 1, "master drain complete, shutting down");

    shutdown_requested_ = true;

    if (heartbeat_check_thread_.joinable()) {
        heartbeat_check_running_ = false;
        // 持锁 notify（waiter 无超时谓词 wait 依赖 5s 周期兜底，锁外 notify
        // 落空会把关闭拖慢最多一个周期——统一持锁化，下同）。
        {
            std::lock_guard<std::mutex> lk(heartbeat_check_mutex_);
            heartbeat_check_cv_.notify_all();
        }
        heartbeat_check_thread_.join();
    }

    if (attr_timeout_check_thread_.joinable()) {
        attr_timeout_check_running_ = false;
        {
            std::lock_guard<std::mutex> lk(attr_timeout_check_mutex_);
            attr_timeout_check_cv_.notify_all();
        }
        attr_timeout_check_thread_.join();
    }

    if (sched_watchdog_thread_.joinable()) {
        sched_watchdog_running_ = false;
        {
            std::lock_guard<std::mutex> lk(sched_watchdog_mutex_);
            sched_watchdog_cv_.notify_all();
        }
        sched_watchdog_thread_.join();
    }

    if (reactor_) {
        DataService::instance()->stop_data_server();

        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
    }

    // clear 触发全量 ~Database（WBQ drain + 关文件重活）：swap 出锁外析构
    //（§13.3；停机路径虽已无并发争用，守规范一致性）。
    {
        CMUnorderedMap<CMString, CMSharedPtr<Database>> doomed;
        {
            std::unique_lock<std::shared_mutex> db_lk(db_instances_mutex_);
            doomed.swap(db_instances_);
        }
    }

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        conn_to_worker_.clear();
        worker_to_conn_.clear();
    }

    // RunSummary：全部常驻线程已 join，停 tick 线程（封闭统计窗口；summary
    // 由 stop_impl 尾部输出）。须在 worker_manager_ 仍存活时停（alive_fn 捕 this）。
    if (run_metrics_) run_metrics_->stop();

    running_ = false;
}

bool MasterAgent::is_running() const {
    return running_;
}

namespace {

// __fly_db__ 编码参数 → db_path。三种格式：
//   v2（现行）__fly_db2__:{uid}:{db_path}（data_path 是 db 级属性存 _DB_META）
//   旧 4 段 __fly_db__:{uid}:{db_path}:{data_path}（与 executor.py 对齐）
//   旧 3 段 __fly_db__:{db_path}:{data_path}
CMString parse_db_arg(const CMString& arg) {
    constexpr size_t kPrefixLen = 11;  // "__fly_db__:"
    if (arg.compare(0, kPrefixLen, "__fly_db__:") != 0) {
        // v2 tag：rest = {uid}:{db_path}，取第二段。
        constexpr size_t kV2PrefixLen = 12;  // "__fly_db2__:"
        if (arg.compare(0, kV2PrefixLen, "__fly_db2__:") != 0) return "";
        const CMString rest = arg.substr(kV2PrefixLen);
        auto p = rest.find(':');
        return p == CMString::npos ? rest : rest.substr(p + 1);
    }
    const CMString rest = arg.substr(kPrefixLen);
    CMVector<CMString> parts;
    size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        auto p = rest.find(':', start);
        if (p == CMString::npos) break;
        parts.push_back(rest.substr(start, p - start));
        start = p + 1;
    }
    parts.push_back(rest.substr(start));
    if (parts.size() == 4) return parts[2];  // 新格式：uid : db_path : data
    if (parts.size() >= 2) return parts[1];  // 旧格式：db_path : data
    return "";
}

// __fly_db__ 编码参数 → (uid, db_path)。v2 与旧 4 段有 uid；旧 3 段无 uid
// （返回空 uid——restart 视作不可解析，文件级拒绝）。
std::pair<CMString, CMString> parse_db_arg_uid(const CMString& arg) {
    constexpr size_t kV2PrefixLen = 12;  // "__fly_db2__:"
    if (arg.compare(0, kV2PrefixLen, "__fly_db2__:") == 0) {
        const CMString rest = arg.substr(kV2PrefixLen);
        auto p = rest.find(':');
        if (p == CMString::npos) return {"", ""};
        return {rest.substr(0, p), rest.substr(p + 1)};
    }
    CMString db_path = parse_db_arg(arg);
    if (db_path.empty()) return {"", ""};
    constexpr size_t kPrefixLen = 11;  // "__fly_db__:"
    const CMString rest = arg.substr(kPrefixLen);
    size_t first = rest.find(':');
    if (first == CMString::npos) return {"", db_path};
    size_t second = rest.find(':', first + 1);
    if (second == CMString::npos) return {"", db_path};       // 旧 3 段：无 uid
    return {rest.substr(0, first), db_path};                   // 旧 4 段
}

}  // namespace

void MasterAgent::submit_task(uint64_t task_id, const TaskSubmissionSpec& spec) {
    // Task db 归属兜底推导（单一真相点：master 本地 / worker 转发 / restart 重投
    // 全部经此）：显式 owner 为空时取 args_ 中第一个 __fly_db__ 编码参数。
    TaskSubmissionSpec effective = spec;
    if (effective.owner_db_path_.empty()) {
        bool first_arg_is_db = false;
        for (size_t i = 0; i < effective.args_.size(); ++i) {
            CMString db = parse_db_arg(effective.args_[i]);
            if (db.empty()) continue;
            effective.owner_db_path_ = db;
            first_arg_is_db = (i == 0);
            break;
        }
        if (!first_arg_is_db) {
            // 开发规范：task 第一个参数必须是归属 db 对象（DEVELOPMENT_GUIDELINES
            // "Task db 归属规则"节）。偏离仅 WARN 不阻断——归属仍正确推导。
            WARN("task {} ('{}') does not take its owner db as the first argument "
                 "(resolved owner={}); convention: def task(db, ...) — see "
                 "DEVELOPMENT_GUIDELINES 'Task db ownership rule'",
                 task_id, effective.name_, effective.owner_db_path_);
        }
    }
    INFO("submit_task: id={}, name={}, owner_db={}, attr_timeout={}, vars={}",
         task_id, effective.name_, effective.owner_db_path_,
         effective.attribute_timeout_, effective.vars_.size());

    // Var existence check (advisory only — does not affect scheduling).
    // vars are FULL names (db_path:short_name); split each to locate the Database.
    if (!effective.vars_.empty()) {
        for (const auto& full_var : effective.vars_) {
            auto [db_path, short_name] = split_full_name(full_var);
            if (db_path.empty()) continue;
            // 锁内只 find + 拷 shared_ptr（var 查询自保护，D2 拆除）。
            CMSharedPtr<Database> db;
            {
                std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
                auto db_it = db_instances_.find(db_path);
                if (db_it != db_instances_.end()) db = db_it->second;
            }
            if (db && !db->master_has_var(short_name)) {
                WARN("task {} declares var '{}' but it does not exist on master (db={})",
                     task_id, short_name, db_path);
            }
        }
    }

    TaskRequirements reqs;
    reqs.capabilities_ = effective.required_capabilities_;
    reqs.timeout_seconds_ = effective.attribute_timeout_;
    reqs.priority_ = effective.priority_;
    // create_task + add_task 必须原子：两者分属 TaskManager / DependencyGraph 两个
    // 独立锁结构，若与 on_task_complete 的 remove_task + update_task_status 交错，
    // 会导致 graph 与 metadata 的完成计数永久分叉（COMPLETED-MISMATCH 卡死）。
    // schedule_mutex_ 串行化所有 task 生命周期复合操作；锁内只做状态变更，不含
    // 网络/调度/Dataservice，并发度影响可忽略。schedule_tasks() 留在锁外（它自身
    // 获取此锁，锁内调用会重入死锁）。
    {
        std::lock_guard<std::mutex> lk(schedule_mutex_);
        metadata_->create_task(task_id, effective);
        graph_->add_task(task_id, effective.inputs_, reqs);
    }
    // monitor 落盘（锁外非阻塞入队）：SUBMIT 事件 + PENDING 行（含 dbs 解析）。
    record_task_snapshot(task_id);
    if (metrics_db_) metrics_db_->record_task_event(task_id, 0, "SUBMIT", effective.name_);

    // Pre-fetch dependency locations at submit time (earliest possible point).
    {
        auto ds = DataService::instance();
        // dep_loc 锁外预取再一次性写入（临界区只含内存插入）。选取用排序后
        // 首选（storage 优先/死 holder 排尾）而非 front()——单位置缓存若
        // 恰好选中死 holder，worker TIER2 会在其上重试满 30s 网络期限。
        CMUnorderedMap<CMString, CachedLocation> locations;
        for (const auto& dep : effective.inputs_) {
            auto replicas = ds->lookup_all_remote_idx(dep);
            if (!replicas.empty() && replicas.front().worker_id_ != 0 &&
                !replicas.front().host_.empty()) {
                const auto& best = replicas.front();
                locations[dep] = {best.worker_id_, best.host_, best.port_};
                DBG("[DEP-LOC] submit-time: task={} obj={} worker={}", task_id, dep, best.worker_id_);
            }
        }
        if (!locations.empty()) {
            task_dependency_locations_.update(task_id,
                [&](CMUnorderedMap<CMString, CachedLocation>& inner) {
                    for (auto& [dep, loc] : locations) {
                        inner[dep] = std::move(loc);
                    }
                });
        }
    }

    {
        bool is_ready = graph_->is_task_ready(task_id);
        auto pending = graph_->get_pending_tasks();
        auto ready = graph_->get_ready_tasks();
        auto deps = graph_->get_task_dependencies(task_id);
        DBG("[DEP] submit: id={} name={} deps={} ready={} pending={} is_ready={}",
             task_id, effective.name_, deps.size(), ready.size(), pending.size(), is_ready);
        for (const auto& dep : deps) {
            DBG("[DEP]   dep={} data_ready={}", dep, graph_->is_data_ready(dep));
        }
    }

    schedule_tasks();
}

void MasterAgent::submit_task(uint64_t task_id, const CMString& name,
                               const CMString& module, const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& outputs,
                               const CMVector<CMString>& required_capabilities,
                               float attribute_timeout,
                               const CMString& write_context_hash,
                               const CMVector<CMString>& vars,
                               int priority,
                               const CMString& owner_db_path) {
    // 位置参数便利重载：组装 spec 转发到主签名，避免每处调用点手写组装。
    TaskSubmissionSpec spec;
    spec.name_ = name;
    spec.module_ = module;
    spec.args_ = args;
    spec.inputs_ = inputs;
    spec.outputs_ = outputs;
    spec.required_capabilities_ = required_capabilities;
    spec.attribute_timeout_ = attribute_timeout;
    spec.write_context_hash_ = write_context_hash;
    spec.vars_ = vars;
    spec.priority_ = priority;
    spec.owner_db_path_ = owner_db_path;
    submit_task(task_id, spec);
}

// locality 预计算（schedule_mutex_ 锁外）：DataService 查询（get_remote_size/
// get_remote_workers）自带锁，不依赖 schedule_mutex_。锁外算好 hint 后，持锁注入
// graph + schedule_all_available，缩短 schedule_mutex_ 持锁时间（reactor 线程与
// attr-tick 线程的竞争窗口）。
// hint 略陈旧无害：remote_idx 更新会再次触发 schedule_tasks()，预计算重做；
// set_task_locality_hint 对已不存在的 task 静默忽略，新就绪 task 下次补算。
void MasterAgent::compute_locality_hints(bool locality_on) {
    if (!locality_on) return;
    auto ready_snapshot = graph_->get_ready_tasks();
    if (ready_snapshot.empty()) return;
    auto ds = DataService::instance();
    for (uint64_t tid : ready_snapshot) {
        auto deps = graph_->get_task_dependencies(tid);
        if (deps.empty()) continue;  // 无依赖，hint 留空（scheduler 退 FIFO）
        CMUnorderedMap<uint64_t, int64_t> acc;  // worker_id → 持有输入累计字节数
        for (const auto& obj : deps) {
            int64_t sz = ds->get_remote_size(obj);
            auto holders = ds->get_remote_workers(obj);
            for (uint64_t h : holders) {
                acc[h] += sz;
            }
        }
        graph_->set_task_locality_hint(tid,
            CMVector<std::pair<uint64_t, int64_t>>(acc.begin(), acc.end()));
    }
}

// 统一的「判死 → 持久化」收尾（依赖不可解 / 属性死锁两处同构）：
// 组 error → make_failed_record → graph 摘除 → metadata 记失败 → 持久化。
void MasterAgent::fail_and_persist_tasks(
        const CMVector<uint64_t>& task_ids,
        const std::function<CMString(uint64_t)>& make_error) {
    for (uint64_t task_id : task_ids) {
        CMString error_msg = make_error(task_id);
        FailedTaskRecord record = make_failed_record(task_id, error_msg);
        graph_->remove_task(task_id);
        metadata_->fail_task(task_id, error_msg);
        persist_failed_task(record);
        ERR("Task {} failed: {}", task_id, error_msg);
    }
}

void MasterAgent::schedule_tasks() {
    if (draining_.load()) return;

    // 每次调度前从 Config 同步开关，使运行时 set_int("locality_scheduling_enabled")
    // 即时生效（无需重启进程）。scheduler 启用后消费 master 预计算的 locality_hint_ 算分。
    bool locality_on =
        Config::instance()->get_int("locality_scheduling_enabled") == 1;
    compute_locality_hints(locality_on);

    CMVector<uint64_t> ready;
    CMVector<uint64_t> idle;
    {
        // 决策、assign（send + metadata 登记）与两处判死检测必须在同一
        // schedule_mutex_ 临界区：决策即 graph_->remove_task（task 离开 ready），
        // metadata 的 RUNNING 登记在 assign 尾部——若 assign 或检测出锁，
        // 「依赖不可解检测」会在「已决策未登记」窗口看到 ready 空 + 无 RUNNING，
        // 把 pending 链整批误判 Unresolvable 失败（qa solver 3 case 实测复现，
        // 2026-08-18；回归守护 UnresolvableDetectionDoesNotFireDuringAssignFlight）。
        std::lock_guard<std::mutex> lock(schedule_mutex_);
        ready = graph_->get_ready_tasks();
        idle = worker_manager_->get_idle_workers();

        if (ready.empty() && !graph_->get_pending_tasks().empty()) {
            auto pending = graph_->get_pending_tasks();
            DBG("[SCHED] schedule_tasks: ready={}, idle={}, pending={} (first_pending={}) "
                "thread={}", ready.size(), idle.size(), pending.size(),
                pending.empty() ? 0 : pending[0],
                std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
        }

        scheduler_->set_locality_preference(locality_on);

        auto results = scheduler_->schedule_all_available();
        for (const auto& result : results) {
            if (result.scheduled_) {
                assign_task_to_worker(result.task_id_, result.worker_id_);
            }
        }

        // 诊断：ready + idle 都非空但 schedule_all_available 没调出任何 task
        // —— 指向 scheduler 内部 capability/locality 决策问题或 worker 状态不一致。
        if (results.empty() && !ready.empty() && !idle.empty()) {
            // 列出 ready task id（前 10 个）便于定位是哪些 task 卡住。
            std::string ready_ids;
            for (size_t i = 0; i < ready.size() && i < 10; ++i) {
                ready_ids += std::to_string(ready[i]);
                if (i + 1 < ready.size() && i + 1 < 10) ready_ids += ",";
            }
            WARN("[SCHED-DIAG] no task scheduled but ready=[{}] idle={} "
                 "(worker_status: {})", ready_ids, idle.size(),
                 worker_manager_->debug_worker_status());
            // 检查每个 ready task 的 requirements，定位为何 select_best_worker 返回 0。
            for (size_t i = 0; i < ready.size() && i < 5; ++i) {
                uint64_t tid = ready[i];
                auto reqs = graph_->get_task_requirements(tid);
                WARN("[SCHED-DIAG]   task={} caps={} locality_hint={} timeout={}",
                     tid, reqs.capabilities_.size(), reqs.locality_hint_.size(),
                     reqs.timeout_seconds_);
            }
            Logger::instance()->flush();
        }

        // 依赖不可解检测：上游 task 失败导致数据被清理后，依赖该数据的 pending
        // task 永远无法就绪。此时若 ready 空（无 task 可调度）且无 running task
        // （无 task 可能产出该数据），应立即判定这些 pending task 失败，而非空等。
        // 这与属性死锁（fail_unscheduleable_tasks 开关控制）是不同的失败模式，
        // 不受该开关影响 —— 数据依赖丢失是确定性的，应即时失败并持久化供 restart。
        {
            auto pending = graph_->get_pending_tasks();
            if (!pending.empty()) {
                auto ready_now = graph_->get_ready_tasks();
                if (ready_now.empty() && !metadata_->has_tasks_with_status(TaskStatus::RUNNING)) {
                    fail_and_persist_tasks(pending, [this](uint64_t task_id) {
                        auto deps = graph_->get_task_dependencies(task_id);
                        CMString dep_list;
                        for (size_t i = 0; i < deps.size(); i++) {
                            if (i > 0) dep_list += ",";
                            dep_list += deps[i];
                        }
                        return "Unresolvable data dependencies: [" + dep_list + "]";
                    });
                }
            }
        }

        if (Config::instance()->get_int("fail_unscheduleable_tasks") == 1) {
            // 属性死锁检测：仅当集群中无任何 worker（含 BUSY）具备所需属性，且无
            // running task（即没有 worker 可能通过 set_worker_property 动态获得属性）
            // 时，才 fail 掉死等(timeout<0)的 task。限时(>=0)的会被降级调度，不在此处 fail。
            bool has_running = metadata_->has_tasks_with_status(TaskStatus::RUNNING);
            CMVector<uint64_t> deadlocked;
            for (uint64_t task_id : graph_->get_ready_tasks()) {
                const auto requirements = graph_->get_task_requirements(task_id);
                if (requirements.capabilities_.empty()) continue;
                // 仅死等(timeout<0)的 task 才适用属性死锁 fail；timeout>=0 的会被降级调度
                if (requirements.timeout_seconds_ >= 0.0f) continue;
                // 有 running task 时，worker 仍可能动态获得属性，不能判定死锁
                if (has_running) continue;

                if (!worker_manager_->has_worker_with_all_capabilities(requirements.capabilities_)) {
                    deadlocked.push_back(task_id);
                }
            }
            fail_and_persist_tasks(deadlocked, [this](uint64_t task_id) {
                auto requirements = graph_->get_task_requirements(task_id);
                CMString cap_list;
                for (size_t i = 0; i < requirements.capabilities_.size(); i++) {
                    if (i > 0) cap_list += ",";
                    cap_list += requirements.capabilities_[i];
                }
                return "No worker with required capabilities: [" + cap_list + "]";
            });
        }
    }
}

void MasterAgent::update_dependency_location_cache(const CMString& object_name, uint64_t worker_id, const CMString& host, int32_t port) {
    // Find pending tasks that depend on this data and cache the location.
    auto pending = graph_->get_pending_tasks();
    for (uint64_t task_id : pending) {
        auto deps = graph_->get_task_dependencies(task_id);
        for (const auto& dep : deps) {
            if (dep == object_name) {
                task_dependency_locations_.update(task_id,
                    [&](CMUnorderedMap<CMString, CachedLocation>& inner) {
                        inner[object_name] = {worker_id, host, port};
                    });
                DBG("[DEP-LOC] cached: task={} obj={} worker={}", task_id, object_name, worker_id);
                break;
            }
        }
    }
}

void MasterAgent::assign_task_to_worker(uint64_t task_id, uint64_t worker_id) {
    INFO("assign_task: task={} to worker={}", task_id, worker_id);

    uint64_t conn_id;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        auto conn_it = worker_to_conn_.find(worker_id);
        if (conn_it == worker_to_conn_.end()) {
            ERR("worker not found: {}", worker_id);
            return;
        }
        conn_id = conn_it->second;
    }

    TaskAssignMessage msg;
    msg.task_id_ = task_id;
    // task 完整提交字段统一从 metadata_->get_task(id)->submission_ 读取（shared_ptr
    // 快照，不可变读线程安全），取代原先 name 从 metadata、module/args/vars 从
    // 并行 map 的两段式上锁拷贝。
    auto task_opt3 = metadata_->get_task(task_id);
    if (task_opt3) {
        const auto& s = task_opt3->submission_;
        msg.task_name_ = s.name_;
        msg.task_module_ = s.module_;
        msg.args_ = s.args_;
        msg.write_context_hash_ = s.write_context_hash_;

        // Inline declared vars into the TaskAssignMessage so the worker receives
        // them in one shot (no extra round-trip). vars are FULL names; split each
        // to locate the Database and fetch the short-named value.
        for (const auto& full_var : s.vars_) {
            auto [db_path, short_name] = split_full_name(full_var);
            if (db_path.empty()) continue;
            // 锁内只 find + 拷 shared_ptr；value 拷贝移出容器锁（D2 拆除）。
            CMSharedPtr<Database> db;
            {
                std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
                auto db_it = db_instances_.find(db_path);
                if (db_it != db_instances_.end()) db = db_it->second;
            }
            if (!db) continue;
            auto [found, value, type_name] = db->master_get_var(short_name);
            if (found && value) {
                VarPayload vp;
                vp.var_name = full_var;  // keep the full name on the wire
                vp.value.assign(value->data(), value->size());
                vp.type_name = type_name;
                msg.var_payloads_.push_back(std::move(vp));
            }
        }
    }

    if (task_opt3) {
        const auto& s = task_opt3->submission_;

        // Populate dependency locations from cache + direct lookup.
        auto ds = DataService::instance();
        {
            // take：消费式读取（原子取走该 task 的缓存位置）。
            auto cached = task_dependency_locations_.take(task_id);
            if (cached.has_value()) {
                for (const auto& [dep, loc] : *cached) {
                    // role 取权威 registry 当前值（缓存不携带，避免陈旧）。
                    DataLocation dl{dep, loc.worker_id, loc.host, loc.port,
                                    ds->is_storage_worker(loc.worker_id) ? 1 : 0};
                    msg.dependency_locations_.push_back(std::move(dl));
                }
            }
        }
        // Also look up any dependencies not in cache (e.g. ready tasks that weren't pending).
        for (const auto& dep : s.inputs_) {
            bool already_cached = false;
            for (const auto& loc : msg.dependency_locations_) {
                if (loc.object_name == dep) {
                    already_cached = true;
                    break;
                }
            }
            if (!already_cached) {
                // 全副本 + 排序（storage 优先/死 holder 排尾）：预取只带单个
                // 副本时，若恰好是死 holder，worker TIER2 会在它上面重试满
                // 30s 网络期限才进 TIER3（接管场景实测卡死根因）。
                for (const auto& loc : ds->lookup_all_remote_idx(dep)) {
                    if (loc.worker_id_ == 0 || loc.host_.empty()) continue;
                    msg.dependency_locations_.push_back({dep, loc.worker_id_, loc.host_, loc.port_,
                                                         loc.storage_only_ ? 1 : 0});
                }
            }
        }
    }

    reactor_->send(conn_id, msg);
#ifdef FLY_ENABLE_TEST_HOOKS
    // 钩子触发点：send 之后、metadata_ 赋值之前（scheduler 线程持 schedule_mutex_）。
    // 测试在此用 std::latch 阻塞，让 reactor 线程的 on_task_complete 的 complete_task
    // 抢先执行，验证「无冗余 assign 覆盖完成」（原 Problem 1 竞态的回归守护）。
    if (assign_task_send_hook_for_testing_) {
        assign_task_send_hook_for_testing_(task_id, worker_id);
    }
#endif

    metadata_->assign_task(task_id, worker_id);
    // monitor 落盘：ASSIGN 事件 + RUNNING 行（ready_ms 此刻可回查 graph）。
    record_task_snapshot(task_id);
    if (metrics_db_) {
        metrics_db_->record_task_event(task_id, worker_id, "ASSIGN");
    }
    // 注：此处不再调用 worker_manager_->assign_task(worker_id, task_id)。
    // scheduler 在选中的瞬间已通过 TaskScheduler::schedule_next（task_scheduler.cpp:68）
    // 把 worker 设为 BUSY 并记录 current_task_id —— 该调用发生在本函数 reactor_->send
    // 之前，是赋值的唯一权威来源。此处再次 assign 是冗余，且会与 on_task_complete 的
    // complete_task 交错：worker 极快完成时，complete_task 先把 worker 设回 IDLE，本函数
    // 的冗余 assign 随即覆盖回 BUSY(current=已完成 task)，导致 worker 永久卡 BUSY、
    // get_idle_workers 永不返回它（Problem 1）。complete_task 只能在 worker 收到
    // TaskAssign（即本函数 send）之后触发，故 schedule_next 的赋值必先于 complete_task，
    // 无同 task 竞态；跨 task 重新赋值是 worker 完成后的正常复用。
}

void MasterAgent::heartbeat_check_loop() {
    while (heartbeat_check_running_) {
        {
            std::unique_lock<std::mutex> lock(heartbeat_check_mutex_);
            heartbeat_check_cv_.wait_for(lock, std::chrono::seconds(5),
                                          [this]{ return !heartbeat_check_running_.load(); });
        }

        // SIGTERM 优雅退出：信号灯置位 → 触发完整 stop() 三阶段 drain（≤5s 延迟）。
        if (graceful_shutdown_signalled()) {
            trigger_graceful_shutdown();
        }

        if (running_ && !draining_.load()) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

            // 断连宽限中的 worker 豁免心跳判死（心跳缺失是断连的自然结果，
            // 判死由宽限计时器统一负责，防 120s 心跳超时与宽限窗口撞车抢跑）。
            CMVector<uint64_t> grace_exempt;
            grace_deadlines_.with_lock([&](const auto& m) {
                for (const auto& [wid, deadline] : m) {
                    grace_exempt.push_back(wid);
                }
            });

            heartbeat_monitor_->check_all_workers(timestamp, grace_exempt);

            check_expected_worker_timeouts(timestamp);
            check_grace_deadlines(timestamp);
            // 存储接管超时兜底（storage_takeover_fail_timeout，默认 60s）：
            // 到期幂等重判全灭与否（ack 已到则无动作），防止接管失败时
            // 等待 task 永久悬挂。
            check_takeover_deadlines(timestamp);
            // 自动补齐存储节点（auto_storage_nodes_enabled，默认关；
            // auto_storage_check_interval 节流，默认 30s）。
            check_storage_nodes(timestamp);
            // 挂起重复注册的 deadline 兜底（探测 15s 无结论 → 保守拒绝）。
            check_dup_register_deadlines(timestamp);

            auto dead = heartbeat_monitor_->get_dead_workers();
            for (uint64_t worker_id : dead) {
                WARN("worker timeout: {}", worker_id);

                uint64_t conn = lookup_worker_conn(worker_id);
                if (conn != 0) {
                    ShutdownMessage shutdown;
                    reactor_->send(conn, shutdown);
                }
            }
        }
    }
}

void MasterAgent::attr_timeout_check_loop() {
    // 周期性触发 schedule_tasks，让限时等待属性的 task 在超时后被降级调度。
    // 检查周期 200ms，满足秒级 float timeout 精度。schedule_tasks 内部会
    // 跳过未超时的 task（select_best_worker 返回 0 被 continue）。
    while (attr_timeout_check_running_) {
        {
            std::unique_lock<std::mutex> lock(attr_timeout_check_mutex_);
            attr_timeout_check_cv_.wait_for(lock, std::chrono::milliseconds(200),
                                             [this]{ return !attr_timeout_check_running_.load(); });
        }

        if (running_ && !draining_.load()) {
            schedule_tasks();
        }
    }
}

void MasterAgent::sched_watchdog_loop() {
    // 调度看门狗：每 3s 检查 ready 队列是否停滞。
    // 若连续 2 轮（6s）ready 队列内容不变（同 count + 同最小 task_id），
    // 判定为调度卡死，输出 WARN + flush（INFO/DEBUG 不 flush，SIGKILL 会丢缓冲）。
    // 同时主动触发一次 schedule_tasks，尝试自愈（看门狗线程不持有 schedule_mutex_
    // 的任何依赖，可安全触发）。
    while (sched_watchdog_running_) {
        {
            std::unique_lock<std::mutex> lock(sched_watchdog_mutex_);
            sched_watchdog_cv_.wait_for(lock, std::chrono::seconds(3),
                                         [this]{ return !sched_watchdog_running_.load(); });
        }
        if (!running_ || draining_.load()) continue;

        auto ready = graph_->get_ready_tasks();
        if (ready.empty()) {
            sched_watchdog_stall_rounds_ = 0;
            continue;
        }
        uint64_t min_id = ready.front();
        for (auto tid : ready) if (tid < min_id) min_id = tid;

        bool stalled = (ready.size() == sched_watchdog_last_ready_count_ &&
                        min_id == sched_watchdog_last_ready_id_);
        if (stalled) {
            sched_watchdog_stall_rounds_++;
        } else {
            sched_watchdog_stall_rounds_ = 0;
        }
        sched_watchdog_last_ready_count_ = ready.size();
        sched_watchdog_last_ready_id_ = min_id;

        if (sched_watchdog_stall_rounds_ >= 2) {
            // 连续 6s ready 队列未前进 → 调度卡死。输出诊断 + flush。
            std::string ready_ids;
            for (size_t i = 0; i < ready.size() && i < 15; ++i) {
                ready_ids += std::to_string(ready[i]);
                if (i + 1 < ready.size() && i + 1 < 15) ready_ids += ",";
            }
            WARN("[SCHED-WATCHDOG] stall detected: ready=[{}] ({} tasks) "
                 "unchanged for {}s. idle_workers={} worker_status={}",
                 ready_ids, ready.size(), sched_watchdog_stall_rounds_ * 3,
                 worker_manager_->get_idle_worker_count(),
                 worker_manager_->debug_worker_status());
            // monitor 落盘：仅首次触发记一次（rounds==2），防 3s 轮询刷爆事件流。
            if (metrics_db_ && sched_watchdog_stall_rounds_ == 2) {
                metrics_db_->record_event("sched", "SCHED_STALLED", 0, min_id,
                                          CMString("ready=") + ready_ids);
            }
            // 检查每个 ready task 的 requirements。
            for (size_t i = 0; i < ready.size() && i < 5; ++i) {
                uint64_t tid = ready[i];
                auto reqs = graph_->get_task_requirements(tid);
                WARN("[SCHED-WATCHDOG]   task={} caps={} hint={} timeout={}",
                     tid, reqs.capabilities_.size(), reqs.locality_hint_.size(),
                     reqs.timeout_seconds_);
            }
            Logger::instance()->flush();
            // 主动触发调度（看门狗线程不持有 schedule_mutex_，安全）。
            schedule_tasks();
        } else if (sched_watchdog_stall_rounds_ == 1) {
            // 第一轮停滞：也 flush 一次，确保 ready 队列状态落盘（即使后续恢复）。
            Logger::instance()->flush();
        }
    }
}

void MasterAgent::on_worker_register(uint64_t conn_id, const RegisterMessage& msg) {
    uint64_t worker_id = msg.worker_id_;
    // 同 id 新化身（手动重启/外部唤起重投）不继承上一任的关停归类标记：
    // 旧标记残留会把新化身的意外断连误判为正常退出。
    shutdown_pending_workers_.erase(worker_id);
    exit_confirmed_workers_.erase(worker_id);
#ifdef FLY_ENABLE_TEST_HOOKS
    // 测试钩子：吞掉本条注册（模拟消息/ack 丢失，P3-23 重发兜底的确定性构造）。
    if (drop_next_register_for_testing_.exchange(false)) {
        WARN("[TEST-HOOK] dropping REGISTER from worker_id={} (conn {})", worker_id, conn_id);
        return;
    }
#endif

    // 重复注册防护（先到先得，用户确认语义）：该 worker_id 已有活跃连接——
    // 典型为网络分区恢复的旧实例与手动重启（AGENT::0006 命令）的新实例竞态。
    // 判定不依赖时序假设（「旧 conn 的 EOF 必然先于新注册处理」在高负载下
    // 不成立——EOF 处理被饿过 worker 重连间隔时，正常重连会被误拒，
    // DisconnectReconnectsAndReports 在 50 轮稳定性测试实测）：
    //   - 已探测确认活着（30s 内 ProbeAck）→ 直接拒绝后到者；
    //   - 否则向旧连接发探测并把后到者挂起（deferred）——旧连接死则 EOF
    //     清表并重放注册（正常接受，首连无重发机制故由 master 重放）；
    //     活着则 ProbeAck 到达后拒绝；15s deadline 保守拒绝兜底。
    {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        bool confirmed_alive = false;
        uint64_t existing_conn = 0;
        {
            std::lock_guard<std::mutex> lk(workers_mutex_);
            auto it = worker_to_conn_.find(worker_id);
            if (it != worker_to_conn_.end() && it->second != conn_id) {
                existing_conn = it->second;
                auto confirmed = dup_confirmed_alive_.find(worker_id);
                if (confirmed && now - *confirmed < 30) {
                    confirmed_alive = true;
                }
            }
        }
        if (confirmed_alive) {
            // 当前这条直接拒；probe 期间积压的挂起条目一并拒。
            RegisterAckMessage dup_ack;
            dup_ack.worker_id_ = worker_id;
            dup_ack.master_address_ = host_;
            dup_ack.master_port_ = static_cast<int32_t>(port_);
            dup_ack.duplicate_ = true;
            reactor_->send(conn_id, dup_ack);
            reject_deferred_register(worker_id, "confirmed alive");
            WARN("Duplicate register rejected: worker_id={} confirmed alive on conn {} "
                 "(incoming conn {})", worker_id, existing_conn, conn_id);
            return;
        }
        if (existing_conn != 0) {
            // 探测旧连接活性。发送失败 = 旧 conn 已死（transport 已 reap、
            // fd 已关）——当场接受为重连注册，不等 DISCONNECT 事件触发
            // replay_deferred_register：handler lane 跨连接并行分发下，
            // on_disconnect(existing_conn) 的 replay take 可能已在本挂起条目
            // 插入前执行过（no-op），挂起条目将孤儿化到 15s deadline 被误拒，
            // worker 全程挂死（50 轮稳定性第 16 轮实测，P3-26）。
            WorkerProbeMessage probe;
            probe.worker_id_ = worker_id;
            if (!reactor_->send(existing_conn, probe)) {
                WARN("Probe to existing conn {} failed (dead) — accepting register of "
                     "worker_id={} on conn {} as reconnect",
                     existing_conn, worker_id, conn_id);
                // 清残留映射（等价旧 conn 断连处理完成的清理部分），落入下方
                // 正常注册路径；迟到的 DISCONNECT 因映射已清而 no-op。
                std::lock_guard<std::mutex> lk(workers_mutex_);
                auto w_it = worker_to_conn_.find(worker_id);
                if (w_it != worker_to_conn_.end() && w_it->second == existing_conn) {
                    worker_to_conn_.erase(w_it);
                }
                auto c_it = conn_to_worker_.find(existing_conn);
                if (c_it != conn_to_worker_.end() && c_it->second == worker_id) {
                    conn_to_worker_.erase(c_it);
                }
            } else {
                DeferredRegister dr;
                dr.conn_id_ = conn_id;
                dr.msg_ = msg;
                dr.deadline_ = now + 15;
                deferred_registers_.update(worker_id, [&](DeferredRegister& d) { d = std::move(dr); });
                WARN("Suspicious duplicate register: worker_id={} existing conn {} — probing "
                     "liveness, incoming register (conn {}) deferred",
                     worker_id, existing_conn, conn_id);
                // 插入后复核（封死「插入晚于 take」的孤儿窗口）：旧 conn 若已
                // 死透，on_disconnect 的 replay 可能先于本插入执行过——就地
                // 自重放（重放路径再进 on_worker_register 时 probe 必失败，
                // 走上面的当场接受分支，无递归风险）。
                if (!reactor_->is_connected(existing_conn)) {
                    replay_deferred_register(worker_id);
                }
                return;
            }
        }
    }

    // 唤起占位符转正：expected 锁持锁跨越「转正 → 进 WorkerManager」全程，
    // 与 snapshot_worker_pool 的采样构成同一临界区口径——采样绝不跨
    // 「已离开占位表、尚未进池」的过渡态（否则两次独立采样会把过渡态
    // worker 两边都漏计，容量瞬时少计导致预检误判池不足）。
    // 锁内各段保持既有顺序（Ack 先于池可见性：TCP 同连接保序杜绝 assign
    // 抢跑）；锁序恒为 expected → workers_mutex_/manager，无反向获取路径，
    // 无死锁环。锁外仅保留纯日志/monitor 落盘。
    // role 静态身份透传（storage_only 在 get_idle_workers 层退出调度候选）。
    WorkerRole role = static_cast<WorkerRole>(msg.role_);
    if (role != WorkerRole::HYBRID && role != WorkerRole::STORAGE_ONLY) {
        WARN("Worker {} reported unknown role {} — treating as hybrid", worker_id,
             static_cast<int>(msg.role_));
        role = WorkerRole::HYBRID;
    }
    expected_worker_ids_.with_lock([&](auto& m) {
        m.erase(worker_id);  // 占位符转正（该 worker 已注册）

        {
            std::lock_guard<std::mutex> lk(workers_mutex_);
            conn_to_worker_[conn_id] = worker_id;
            worker_to_conn_[worker_id] = conn_id;
        }

        // 断连宽限内的重连：task 在 worker 上存活（RUNNING 保留），重连后将正常
        // 上报 Complete/Failed——保留 BUSY 与 current_task_id_（覆盖为 IDLE 会让调度器
        // 立即派新 task，与迟到上报的状态迁移竞争）。宽限外注册维持全新语义。
        DataService::instance();
        if (msg.data_server_port_ > 0) {
            DataService::instance()->register_worker(worker_id, msg.data_server_host_,
                                                      msg.data_server_port_,
                                                      role == WorkerRole::STORAGE_ONLY);
        }

        // Ack 先于 scheduler 可见性发送（用户确认语义：assign 不应在 worker 收到
        // 注册确认前发生——原顺序 register_worker 后 scheduler 即可 assign，
        // TaskAssign 可抢在 RegisterAck 之前到达 worker，执行中的写注册/上报
        // 只能走缓冲）。TCP 同连接保序：Ack 先发必先到，此处彻底关死抢跑窗口。
        RegisterAckMessage ack;
        ack.worker_id_ = worker_id;
        ack.master_address_ = host_;
        ack.master_port_ = static_cast<int32_t>(port_);
        reactor_->send(conn_id, ack);

        // scheduler 可见性（在此之后 assign 才可能发生）。
        bool in_grace = (grace_deadlines_.erase(worker_id) > 0);
        if (in_grace) {
            worker_manager_->register_worker_reconnect(worker_id, host_, port_, msg.attributes_,
                                                        msg.hostname_, msg.ip_address_, role);
            INFO("Worker re-connected within grace: worker_id={}, conn_id={} "
                 "(task state preserved)", worker_id, conn_id);
        } else {
            worker_manager_->register_worker(worker_id, host_, port_, msg.attributes_,
                                              msg.hostname_, msg.ip_address_, role);
        }
    });

    if (msg.data_server_port_ > 0) {
        INFO("Worker registered: worker_id={}, conn_id={}, hostname={}, data_server={}:{}, role={}",
             worker_id, conn_id, msg.hostname_, msg.data_server_host_, msg.data_server_port_,
             role == WorkerRole::STORAGE_ONLY ? "storage_only" : "hybrid");
    }
    // monitor 落盘：workers 表 upsert（含重连注册）+ REGISTER 事件。
    if (metrics_db_) {
        monitor_self_event();  // 事件采样：worker 注册时刻
        CMString attrs;
        for (size_t i = 0; i < msg.attributes_.size(); ++i) {
            if (i) attrs += ",";
            attrs += msg.attributes_[i];
        }
        metrics_db_->record_worker_registered(
            worker_id, msg.hostname_, msg.ip_address_,
            role == WorkerRole::STORAGE_ONLY ? "storage_only" : "hybrid", attrs);
    }

    // storage_only 上线 = 自动补齐的成功信号：清该 host 的 spawn 占位与
    // 失败计数（下轮检测将该 host 视为已覆盖）。
    if (role == WorkerRole::STORAGE_ONLY && !msg.hostname_.empty()) {
        pending_storage_spawns_.erase(msg.hostname_);
        storage_spawn_failures_.erase(msg.hostname_);
    }

    // 流程 message：worker 上线（集群扩容里程碑）。
    MSG("AGENT::0001", 1, "worker {} online (hostname={}, {}:{})",
        worker_id, msg.hostname_, msg.data_server_host_, msg.data_server_port_);

    // 补发当前配额给新 worker（worker 子进程是全新进程，拿不到 master 脚本设的配额）。
    MessageLimitSyncMessage limit_msg;
    fly::MessageRegistry::instance().get_all_limits(
        limit_msg.global_limit_, limit_msg.domain_keys_, limit_msg.domain_values_,
        limit_msg.id_keys_, limit_msg.id_values_);
    reactor_->send(conn_id, limit_msg);

    // 新 worker 注册后，ready 的 task 可能可调度（含等待属性的 task）
    schedule_tasks();
}

void MasterAgent::on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg) {
    uint64_t worker_id = msg.worker_id_;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    worker_manager_->set_heartbeat(worker_id, timestamp);

    auto worker = worker_manager_->get_worker(worker_id);
    // 终态通用复活：任一终态（异常判死/正常退出）的 worker 心跳到达均说明
    // 活体回归（同 id 新进程注册）。恢复 IDLE + registry 活标记。
    if (worker && !worker_status_alive(worker->get().status_)) {
        worker_manager_->update_worker_status(worker_id, WorkerStatus::IDLE);
        DataService::instance()->set_worker_alive(worker_id, true);
        INFO("Worker {} revived (heartbeat received after timeout)", worker_id);
        if (metrics_db_) metrics_db_->record_worker_event(worker_id, "REVIVED");
    }

    HeartbeatAckMessage ack;
    ack.worker_id_ = worker_id;
    reactor_->send(conn_id, ack);

    DBG("Heartbeat from worker_id={}", worker_id);
}

// monitor 采样成组上报（独立于心跳的负载通道）：RunSummary 喂样（提取
// epoch/RSS 平行数组，语义与原心跳 RSS 采样一致，仅通道迁移）+ monitor.db
// 落库（(worker_id, epoch_ms) 主键幂等，重复补发不重不漏）。
void MasterAgent::on_monitor_sample(const MonitorSampleMessage& msg) {
    // 事件采样：收到 worker 样本=集群活跃信号（master 侧快照，节流）。
    monitor_self_event();
    if (run_metrics_ && !msg.samples_.empty()) {
        CMVector<uint64_t> epochs, rss;
        epochs.reserve(msg.samples_.size());
        rss.reserve(msg.samples_.size());
        for (const auto& s : msg.samples_) {
            epochs.push_back(s.epoch_ms_);
            rss.push_back(s.proc_rss_bytes_);
        }
        run_metrics_->on_worker_samples(msg.worker_id_, epochs, rss);
    }
    if (metrics_db_) {
        metrics_db_->record_worker_samples(msg.worker_id_, msg.samples_);
    }
    DBG("MonitorSample from worker_id={}: {} samples", msg.worker_id_, msg.samples_.size());
}

// 对象级 IO 明细（尽力而为通道）：转 ObjectIoRecord 落 object_io 表。
void MasterAgent::on_task_io_report(const MonitorTaskIoMessage& msg) {
    if (!metrics_db_ || msg.items_.empty()) return;
    CMVector<ObjectIoRecord> records;
    records.reserve(msg.items_.size());
    for (const auto& item : msg.items_) {
        ObjectIoRecord r;
        r.epoch_ms_ = item.epoch_ms_;
        r.task_id_ = msg.task_id_;
        r.worker_id_ = msg.worker_id_;
        r.is_write_ = item.is_write_ != 0;
        r.object_name_ = item.object_name_;
        r.bytes_ = item.bytes_;
        r.duration_ms_ = item.duration_ms_;
        records.push_back(r);
    }
    metrics_db_->record_object_io(records);
}

// ===== cluster monitor 落盘接线 =====

namespace {

// worker 上报的扩展字段（TaskComplete/TaskFailed 同构）覆盖到行快照。
template <typename MsgT>
static void fill_row_from_report(TaskRow& row, const MsgT& m) {
    row.exec_start_ms_ = m.exec_start_ms_;
    row.exec_end_ms_ = m.exec_end_ms_;
    row.cpu_time_ms_ = m.cpu_time_ms_;
    row.mem_baseline_bytes_ = m.mem_baseline_bytes_;
    row.mem_avg_bytes_ = m.mem_avg_bytes_;
    row.mem_peak_bytes_ = m.mem_peak_bytes_;
    row.read_time_ms_ = m.read_time_ms_;
    row.write_time_ms_ = m.write_time_ms_;
    row.read_bytes_ = m.read_bytes_;
    row.write_bytes_ = m.write_bytes_;
}

const char* task_status_name(TaskStatus s) {
    switch (s) {
        case TaskStatus::PENDING:   return "PENDING";
        case TaskStatus::RUNNING:   return "RUNNING";
        case TaskStatus::COMPLETED: return "COMPLETED";
        case TaskStatus::FAILED:    return "FAILED";
    }
    return "UNKNOWN";
}

// task 关联 db 集合（args 的 __fly_db__ 编码 + inputs_/outputs_ 对象全名前缀），
// 去重排序后逗号连接（GUI 按 LIKE 反查 task↔db）。
CMString collect_task_dbs(const TaskSubmissionSpec& spec) {
    CMUnorderedSet<CMString> dbs;
    for (const auto& a : spec.args_) {
        CMString db = parse_db_arg(a);
        if (!db.empty()) dbs.insert(db);
    }
    auto add_from_obj = [&](const CMString& full) {
        auto pos = full.rfind(':');
        if (pos != CMString::npos && pos > 0) dbs.insert(full.substr(0, pos));
    };
    for (const auto& i : spec.inputs_) add_from_obj(i);
    for (const auto& o : spec.outputs_) add_from_obj(o);
    CMVector<CMString> sorted(dbs.begin(), dbs.end());
    std::sort(sorted.begin(), sorted.end());
    CMString out;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i) out += ",";
        out += sorted[i];
    }
    return out;
}

}  // namespace

// db 磁盘占用落库：detail 编码 "<db_path>|<bytes>"（-1 = 未测得）。
// freeze 点的值为终值（frozen db 不再写入）；stop 收尾对 active db 是
// write_summary_files 的退出补测值。GUI 的 DBs 页据此展示磁盘占用。
void MasterAgent::record_db_du(const CMString& db_path) {
    if (!metrics_db_) return;
    const int64_t bytes = run_metrics_ ? run_metrics_->db_disk_bytes(db_path) : -1;
    metrics_db_->record_event("db", "DB_DU", 0, 0,
                              db_path + "|" + std::to_string(bytes));
}

TaskRow MasterAgent::build_task_row(uint64_t task_id) const {
    TaskRow row;
    row.task_id_ = task_id;
    auto md = metadata_->get_task(task_id);
    if (!md) return row;  // internal task 等无 metadata：返回骨架行（扩展字段由调用点覆盖）
    row.name_ = md->submission_.name_;
    row.module_ = md->submission_.module_;
    row.is_internal_ = md->submission_.module_ == "__fly_internal";
    row.status_ = task_status_name(md->status_);
    row.worker_id_ = md->assigned_worker_id_;
    row.priority_ = md->submission_.priority_;
    row.error_ = md->error_message_;
    row.created_ms_ = md->created_at_;
    row.ready_ms_ = graph_ ? static_cast<uint64_t>(graph_->get_task_ready_epoch_ms(task_id)) : 0;
    row.started_ms_ = md->started_at_;
    row.completed_ms_ = md->completed_at_;
    row.dbs_ = collect_task_dbs(md->submission_);
    return row;
}

void MasterAgent::record_task_snapshot(uint64_t task_id) {
    if (metrics_db_) metrics_db_->record_task(build_task_row(task_id));
}

// master 自监控：每 monitor_report_interval_ms 采一条全维度样本直写 DB
//（worker_id=0，role=master；不经网络消息——master 本地直写）。
void MasterAgent::monitor_self_loop() {
    const int64_t interval_ms = Config::instance()->get_int("monitor_report_interval_ms");
    while (monitor_self_running_.load()) {
        {
            std::unique_lock<std::mutex> lk(monitor_self_mutex_);
            monitor_self_cv_.wait_for(lk, std::chrono::milliseconds(interval_ms),
                                      [this] { return !monitor_self_running_.load(); });
        }
        if (!monitor_self_running_.load()) break;
        if (metrics_db_) {
            CMVector<MonitorSample> one{monitor_self_sampler_.sample_once()};
            {
                std::lock_guard<std::mutex> t(self_sample_throttle_mutex_);
                self_last_sample_ms_ = one.back().epoch_ms_;
            }
            metrics_db_->record_worker_samples(0, one);
        }
    }
}

// master 侧事件采样：cluster 事件时刻的全维度快照（kind=1 直写；与周期采样
// 共用节流——高频事件（task 完成风暴）不刷爆 DB）。
void MasterAgent::monitor_self_event() {
    if (!metrics_db_) return;
    MonitorSample sp = monitor_self_sampler_.sample_once();
    sp.kind_ = 1;
    {
        std::lock_guard<std::mutex> t(self_sample_throttle_mutex_);
        if (sp.epoch_ms_ < self_last_sample_ms_ + static_cast<uint64_t>(self_sample_gap_ms_)) {
            return;
        }
        self_last_sample_ms_ = sp.epoch_ms_;
    }
    metrics_db_->record_worker_samples(0, CMVector<MonitorSample>{sp});
}

// 登记写入该 db 的 worker 元数据（db meta recorded_workers_）。
// 从原 on_data_ready 的非冗余逻辑抽出，供 do_write_register 复用。
void MasterAgent::record_worker_info(const CMString& object_name, const CMString& db_path,
                                      uint64_t worker_id, const CMString& writer_id_in) {
    CMString hostname;
    CMString ip;
    if (worker_id == 0) {
        hostname = ProcessInfo::instance()->hostname();
        ip = host_;
    } else {
        // hostname/ip 统一从 WorkerManager 取（受 mutex_ 保护，消除原并行 map 的无锁访问）。
        hostname = worker_manager_->get_hostname(worker_id);
        ip = worker_manager_->get_ip_address(worker_id);
    }

    if (hostname.empty()) return;

    CMString writer_id = writer_id_in;
    if (writer_id.empty()) {
        CMSharedPtr<Database> db;
        {
            std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
            auto db_it2 = db_instances_.find(db_path);
            if (db_it2 != db_instances_.end()) {
                db = db_it2->second;
            }
        }
        if (db) writer_id = db->get_writer_id();
    }

    auto key = std::make_tuple(db_path, hostname, writer_id);
    // insert 返回是否新插入：入队副作用在容器锁外恰好执行一次，同时消除原
    // recorded_workers_mutex_ → db_instances_mutex_ 嵌套锁。落盘走队列 + 消费点
    // flush（见 pending_worker_infos_ 注释——reactor 线程直接回调 Python 会与
    // 主线程持 GIL 的 wait 类 API 互等死锁）。
    if (recorded_workers_.insert(key)) {
        ::WorkerInfo info;
        info.worker_id_ = worker_id;
        info.writer_id_ = writer_id;
        info.hostname_ = hostname;
        info.ip_address_ = ip;
        info.launch_command_ = "";
        pending_worker_infos_.update(db_path, [&info](CMVector<::WorkerInfo>& v) {
            v.push_back(info);
        });
    }
}

void MasterAgent::flush_worker_infos() {
    if (!record_worker_info_func_) return;
    // take_any 消费式弹出：锁内只做条目移动，回调（Python 落盘）在锁外。
    while (auto entry = pending_worker_infos_.take_any()) {
        auto& [db_path, infos] = *entry;
        for (auto& info : infos) {
            record_worker_info_func_(db_path, info.worker_id_, info.writer_id_,
                                     info.hostname_, info.ip_address_,
                                     info.launch_command_);
        }
    }
}

void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    // 事件采样：task 完成时刻的 master 快照（节流；完成风暴下与周期无异）。
    monitor_self_event();
    size_t written_count = msg.written_objects_.size();
    INFO("Task complete: task_id={}, written_objects={}", msg.task_id_, written_count);

    uint64_t worker_id = msg.worker_id_;

    // 迟到上报防串扰（断连宽限语义）：宽限超时判死后 task 已重排队给别的 worker，
    // 原 worker 若迟到重连并上报——校验 task 当前 assigned worker 与上报者一致，
    // 不符则丢弃（防止把别人正在跑的同 id task 错误标完成/置其 worker IDLE）。
    if (auto t = metadata_->get_task(msg.task_id_)) {
        if (t->assigned_worker_id_ != 0 && t->assigned_worker_id_ != worker_id) {
            WARN("Stale task report dropped: task_id={} reported by worker {} but "
                 "currently assigned to worker {} (worker likely exceeded grace and "
                 "task was re-queued)",
                 msg.task_id_, worker_id, t->assigned_worker_id_);
            if (metrics_db_) {
                metrics_db_->record_task_event(msg.task_id_, worker_id, "STALE_REPORT_DROPPED");
            }
            return;
        }
    } else {
        WARN("Task complete report for unknown task_id={} (already cleaned?)",
             msg.task_id_);
    }

    worker_manager_->complete_task(worker_id);

    DataService::instance();
    auto addr = DataService::instance()->get_worker_address(worker_id);

    if (!msg.is_internal_) {
        bool streaming_mode = (Config::instance()->get_int("dependency_update_mode") == 0);

        for (const auto& wo : msg.written_objects_) {
            if (!streaming_mode) {
                // 非 stream 模式：write register 时只做了校验（provenance/frozen），
                // 可见性登记延迟到此处统一完成（task 级原子性）。
                // db_path 从 object_name_ 反解（"db_path:short_name" 固定前缀格式）。
                graph_->mark_data_ready(wo.object_name_);
                DataService::instance()->update_remote_idx(wo.object_name_, worker_id, addr.host_, addr.port_, wo.size_bytes_);
                update_dependency_location_cache(wo.object_name_, worker_id, addr.host_, addr.port_);
                auto [db_path, short_name] = fly::split_full_name(wo.object_name_);
                if (!db_path.empty()) {
                    // wo.writer_id_ 非空（merge task 上报真实 writer）优先；
                    // 空（普通 task）fallback master Database 的 writer id。
                    record_worker_info(wo.object_name_, db_path, worker_id, wo.writer_id_);
                }
                DBG("Recorded data location (non-stream, task complete): {} -> worker {}", wo.object_name_, worker_id);
            }
        }

        // remove_task + update_task_status 必须原子（与 submit_task 的
        // create_task + add_task 互斥），否则 graph 与 metadata 的完成计数分叉。
        // schedule_mutex_ 串行化 task 生命周期复合操作。schedule_tasks() 留在锁外。
#ifdef FLY_ENABLE_TEST_HOOKS
        // 钩子触发点：获取 schedule_mutex_ 之前（结构稳定点）。complete_task 当前在其
        // 上方（锁外）执行 —— 测试在此 count_down 通知 scheduler 线程「complete_task 已完成」，
        // 让 scheduler 随后的 worker_manager_->assign_task 覆盖回 BUSY（即 Problem 1 竞态）。
        if (on_task_complete_prelock_hook_for_testing_) {
            on_task_complete_prelock_hook_for_testing_(msg.task_id_, worker_id);
        }
#endif
        {
            std::lock_guard<std::mutex> lk(schedule_mutex_);
            graph_->remove_task(msg.task_id_);
            metadata_->update_task_status(msg.task_id_, TaskStatus::COMPLETED);
            remove_persisted_task(msg.task_id_);

            // 非 stream 模式：task 成功 → pending frozen 迁移到 confirmed + 广播。
            // stream 模式下 pending 为空（freeze 已在 on_database_freeze_request 即时确认），此处 no-op。
            // msg.frozen_dbs_ 仅用于日志校验；迁移按 task_id 从 pending 精确提取。
            if (!msg.frozen_dbs_.empty()) {
                DBG("Task complete frozen_dbs (declared): task_id={}, count={}",
                    msg.task_id_, msg.frozen_dbs_.size());
            }
            commit_pending_frozen(msg.task_id_);
        }
        // monitor 落盘（锁外非阻塞）：COMPLETED 终态行（含执行窗口/CPU/内存/IO
        // 扩展字段）+ 事件。
        if (metrics_db_) {
            TaskRow row = build_task_row(msg.task_id_);
            fill_row_from_report(row, msg);
            metrics_db_->record_task(row);
            metrics_db_->record_task_event(msg.task_id_, worker_id, "COMPLETE");
        }
        // task 的 module/args/vars 随 TaskMetadata.submission_ 存活，状态迁移到
        // COMPLETED 即完成生命周期管理，无需单独清理并行 map。
    } else {
        // Internal tasks (backup, etc.) always update remote_idx
        for (const auto& wo : msg.written_objects_) {
            DataService::instance()->update_remote_idx(wo.object_name_, worker_id, addr.host_, addr.port_, wo.size_bytes_);
            // merge worker 的真实 writer 记入 _DB_META（跨进程 load_db 按 idx
            // 文件名恢复的依据；wo.writer_id_ 空时 fallback master Database 的）。
            auto [db_path, short_name] = fly::split_full_name(wo.object_name_);
            if (!db_path.empty()) {
                record_worker_info(wo.object_name_, db_path, worker_id, wo.writer_id_);
            }
            DBG("Internal task: recorded data location: {} -> worker {}", wo.object_name_, worker_id);
        }
        // monitor 落盘：internal task（无 metadata）骨架行 + 扩展字段。
        if (metrics_db_) {
            TaskRow row;
            row.task_id_ = msg.task_id_;
            row.name_ = "__fly_internal";
            row.module_ = "__fly_internal";
            row.is_internal_ = true;
            row.status_ = "COMPLETED";
            row.worker_id_ = worker_id;
            fill_row_from_report(row, msg);
            metrics_db_->record_task(row);
            metrics_db_->record_task_event(msg.task_id_, worker_id, "COMPLETE", "internal");
        }
        // merge task 完成路由（若是 merge task）。
        on_merge_task_complete(msg.task_id_, worker_id, msg.written_objects_);
    }

    schedule_tasks();
    notify_drain_if_active();
}

void MasterAgent::on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg) {
    ERR("Task failed: task_id={}, error={}", msg.task_id_, msg.error_message_);

    uint64_t worker_id = msg.worker_id_;

    // 迟到上报防串扰（同 on_task_complete 的校验语义）。
    if (auto t = metadata_->get_task(msg.task_id_)) {
        if (t->assigned_worker_id_ != 0 && t->assigned_worker_id_ != worker_id) {
            WARN("Stale task-failure report dropped: task_id={} reported by worker {} "
                 "but currently assigned to worker {}",
                 msg.task_id_, worker_id, t->assigned_worker_id_);
            if (metrics_db_) {
                metrics_db_->record_task_event(msg.task_id_, worker_id, "STALE_REPORT_DROPPED");
            }
            return;
        }
    }

    worker_manager_->complete_task(worker_id);
    // fail_task + remove_task 原子（同 on_task_complete 的状态变更段保护）。
    {
        std::lock_guard<std::mutex> lk(schedule_mutex_);
        metadata_->fail_task(msg.task_id_, msg.error_message_);
        graph_->remove_task(msg.task_id_);
    }
    // monitor 落盘（锁外非阻塞）：FAILED 终态行（含部分执行窗口字段）+ 事件。
    if (metrics_db_) {
        TaskRow row = build_task_row(msg.task_id_);
        fill_row_from_report(row, msg);
        metrics_db_->record_task(row);
        metrics_db_->record_task_event(msg.task_id_, worker_id, "FAIL",
                                       msg.error_message_.substr(0, 200));
    }

    // 运行时失败的 task（异常/读不到数据）也应可 restart，与调度时失败的 task 一致。
    // make_failed_record 从 metadata.submission_ 整体拷贝，无需逐字段复制。
    // internal task（__merge_object 等）不在 metadata：无 submission 可还原，
    // 以空 submission 持久化会污染 failed_tasks.bin（restart_failed_tasks 恢复空
    // task），跳过——merge task 的失败由 merge_task_states_ 状态机承载。
    if (metadata_->get_task(msg.task_id_)) {
        FailedTaskRecord record = make_failed_record(msg.task_id_, msg.error_message_);
        persist_failed_task(record);
    }

    // 清理失败 task 已写出的脏对象：worker 已本地撤销（idx ABORT + data truncate），
    // master 据此清理 remote_idx / provenance / 依赖图，并广播 OBJECT_REMOVED
    // 通知其他 worker 清缓存，避免读到失效数据。
    for (const auto& obj : msg.dirty_objects_) {
        DataService::instance()->remove_remote_index(obj);
        auto [db_path, short_name] = fly::split_full_name(obj);
        provenance_erase(db_path, short_name);
        graph_->mark_data_removed(obj);

        if (!db_path.empty()) {
            broadcast_object_removed(db_path, short_name);
        }
        WARN("Dirty object cleaned after task failure: task_id={}, object={}",
             msg.task_id_, obj);
    }

    if (msg.error_type_ == TaskErrorType::WRITE_REGISTRATION_TIMEOUT ||
        msg.error_type_ == TaskErrorType::EXECUTION_ERROR) {
        ERR("FATAL: unrecoverable error (type={}) for task_id={}: {}",
            static_cast<int>(msg.error_type_), msg.task_id_, msg.error_message_);
        // 流程 message：不可恢复 task 失败（ERROR 级，用户必须知道）。
        MSG("TASK::0001", 1, "task {} failed (unrecoverable, type={}): {}",
            msg.task_id_, static_cast<int>(msg.error_type_), msg.error_message_);
    }

    // 非 stream 模式：task 失败 → 按 task_id 回滚 pending frozen（防永久死锁）。
    // stream 模式下 pending 为空，此处 no-op。
    rollback_pending_frozen(msg.task_id_);

    // merge task 失败路由（若是 merge task，让 wait_merge_tasks_complete 收到失败信号）。
    on_merge_task_failed(msg.task_id_, msg.error_message_);

    schedule_tasks();
    notify_drain_if_active();
}

void MasterAgent::on_disconnect(uint64_t conn_id) {
#ifdef FLY_ENABLE_TEST_HOOKS
    if (on_disconnect_entry_hook_for_testing_) {
        on_disconnect_entry_hook_for_testing_(conn_id);
    }
#endif
    uint64_t worker_id = 0;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        auto it = conn_to_worker_.find(conn_id);
        if (it != conn_to_worker_.end()) {
            worker_id = it->second;
            conn_to_worker_.erase(conn_id);
            // 条件 erase：worker_to_conn_ 当前可能已指向重连后的新 conn（本
            // 断开是旧 conn 的迟到事件）——无条件 erase 会误删新映射。
            auto w_it = worker_to_conn_.find(worker_id);
            if (w_it != worker_to_conn_.end() && w_it->second == conn_id) {
                worker_to_conn_.erase(w_it);
            }
        }
    }
    if (worker_id == 0) return;
    // 连接已断：活性确认失效（后续该 wid 的可疑注册需重新探测）。
    dup_confirmed_alive_.erase(worker_id);
    // 旧连接断开 = 挂起注册的「旧 conn 是残留」结论 → 重放（正常接受）。
    // 只在断开的是「既有连接」时重放：后到者自己的 conn 断开（等结论期间
    // 放弃离开）则无 deferred 可取，take 返回空自然 no-op。
    replay_deferred_register(worker_id);

    // worker 断开时，如果正在等 message summary，减少 expected_count 并唤醒 CV，
    // 避免 collect_and_print_message_summary 永远等一个已断开的 worker 的上报。
    {
        std::lock_guard<std::mutex> lk(msg_count_mutex_);
        if (pending_msg_count_.expected_count_ > 0 &&
            pending_msg_count_.received_count_ < pending_msg_count_.expected_count_) {
            pending_msg_count_.expected_count_--;
            DBG("[SUMMARY] worker {} disconnected during summary wait, expected_count now {}",
                worker_id, pending_msg_count_.expected_count_);
            if (pending_msg_count_.received_count_ >= pending_msg_count_.expected_count_) {
                msg_count_cv_.notify_all();
            }
        }
    }

    WARN("Worker disconnected: worker_id={}", worker_id);
    // monitor 落盘：DISCONNECT 事件（drain 期也记——GUI 需要完整关停时序）。
    if (metrics_db_ && worker_id != 0) {
        metrics_db_->record_worker_event(worker_id, "DISCONNECT");
    }
    // 流程 message：worker 掉线（非 drain 期才打，drain 期属正常关闭会刷屏）。
    if (!draining_.load()) {
        MSG("AGENT::0002", 1, "worker {} offline", worker_id);
    }

    // 断连归类（用户裁定：正常退出与异常退出显式分派，不混流）：
    //   ① 正常退出——master 主动关停指令先行（shutdown_pending）或 worker
    //      graceful 声明已到达（WORKER_EXIT，本地 stop 等场景）：走
    //      handle_worker_exit，不进判死链（数据已随 WBQ drain 落盘，
    //      "副本全灭"不适用）。消费即清除标记。
    //   ② 异常——drain 期未标记的断连 / worker_reconnect_timeout=0（断连即死
    //      逃生口）→ handle_worker_death（含 fail_orphan_data_objects）。
    //   ③ 其余——正常运行期意外断连：登记宽限等重连。
    const bool shutdown_pending = shutdown_pending_workers_.erase(worker_id) > 0;
    const bool exit_confirmed = exit_confirmed_workers_.erase(worker_id) > 0;
    int64_t reconnect_grace = Config::instance()->get_int("worker_reconnect_timeout");
    if (shutdown_pending || exit_confirmed) {
        INFO("worker {} exited ({}); running normal-exit handling",
             worker_id,
             shutdown_pending ? "master-initiated shutdown"
                              : "graceful exit confirmed by worker");
        handle_worker_exit(worker_id);
    } else if (draining_.load() || reconnect_grace <= 0) {
        handle_worker_death(worker_id);
    } else {
        auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        grace_deadlines_.update(worker_id, [&](int64_t& t) { t = now + reconnect_grace; });
        // IDLE worker 断连也退出调度候选（BUSY 天然不被调度）：连接已死，assign
        // 必失败；且重连注册保留关联后 task 悬挂（宽限被重连解除、无判死兜底）。
        // 重连注册（register_worker_reconnect）复位 in_grace_ 恢复调度。
        worker_manager_->set_worker_grace(worker_id, true);
        WARN("worker {} disconnected — grace period {}s (tasks stay RUNNING, awaiting reconnect)",
             worker_id, reconnect_grace);
    }

    // Notify stop() that a worker has disconnected（持锁 notify：stop_impl
    // Phase 3 的 workers_drained_cv_ 是 wait_until，锁外 notify 落空会把
    // 断连感知拖到 deadline——worker 主动断连的亚秒收益被偶发打回 2s/10s）。
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        workers_drained_cv_.notify_all();
    }
}

// worker 正常退出（master 主动关停确认 / worker graceful 声明）：与异常判死
// 显式分流的收尾路径（用户裁定语义）。保留清理与等待收敛必需的动作；去除
// 异常路径专属的判死告警与数据全灭 fail——正常关停时 worker 已 drain WBQ，
// 持久对象在盘、可 load_db 恢复，"唯一 holder 退出 = 数据丢失"不成立。
void MasterAgent::handle_worker_exit(uint64_t worker_id) {
    INFO("worker {} exited (master-initiated shutdown confirmed)", worker_id);
    grace_deadlines_.erase(worker_id);
    // RunSummary：退出时刻（该 worker 后续样本不再计入）。
    if (run_metrics_) run_metrics_->on_worker_dead(worker_id);
    // monitor 落盘：EXITED 事件（区别于异常 DEAD——GUI 无需再启发式推导）。
    if (metrics_db_) metrics_db_->record_worker_event(worker_id, "EXITED");

    // 判死联动收敛 pending RPC 期待（同 death 路径：无限等待只被显式失败
    // 信号终结；worker 进程退出即其 RPC/加载的终局信号）。
    settle_pending_for_dead_worker(worker_id);

    CMVector<uint64_t> tasks_to_recover;
    {
        std::lock_guard<std::mutex> lk(schedule_mutex_);
        worker_manager_->update_worker_status(worker_id, WorkerStatus::EXITED);
        tasks_to_recover = metadata_->get_task_ids_by_worker(worker_id);
    }
    DataService::instance()->set_worker_alive(worker_id, false);

    // pending frozen 清理（同 death 路径：防 db 永久"冻结中"死锁）。
    for (uint64_t task_id : tasks_to_recover) {
        rollback_pending_frozen(task_id);
    }

    // 防御分支：正常时序下 drain（或 fast 路径的 fail 善后）已把 RUNNING
    // 清零，此处应为空集、零输出；仅在时序交错（exit 确认早于 drain 完成
    // 观测）时兜底——保证 stop() 永不因残留 RUNNING 悬挂。
    for (uint64_t task_id : tasks_to_recover) {
        std::lock_guard<std::mutex> lk(schedule_mutex_);
        if (metadata_->get_task(task_id) &&
            metadata_->get_task(task_id)->status_ == TaskStatus::RUNNING) {
            metadata_->fail_task(task_id, "worker exited during shutdown");
            graph_->remove_task(task_id);
            record_task_snapshot(task_id);
            WARN("Task failed (worker exited during shutdown): task_id={}", task_id);
        }
    }
    notify_drain_if_active();
}

// worker 正式判死（宽限超时 / drain 期断连 / 断连即死模式）。
// Problem 2 fix：worker DEAD 标记 + task snapshot 必须在 schedule_mutex_ 内原子执行。
// 原实现 DEAD 标记与 snapshot 均在锁外，可与 scheduler（持 schedule_mutex_ 的
// get_idle_workers → schedule_next:68 assign → assign_task_to_worker:762
// metadata assign）交错：snapshot 漏掉刚 assign 到 W 的 task → 该 task 永久孤儿
// （RUNNING@DEAD-W，graph 已 remove，无人恢复，集群容量慢性耗尽）。纳入锁后：
//   - handle_worker_death 先获锁：W 标 DEAD 后释放，scheduler 的 get_idle_workers
//     排除 W，不会把新 task assign 到 W；snapshot 一致无遗漏。
//   - scheduler 先获锁完成 assign：handle_worker_death 等锁后取 snapshot，能看到
//     该 task 并恢复。
// 锁序安全：schedule_mutex_ → worker_manager::mutex_（与 schedule_tasks 内
// get_idle_workers/assign_task 的获取顺序一致，无反向，不死锁）。
void MasterAgent::handle_worker_death(uint64_t worker_id) {
    WARN("worker {} declared dead (grace expired or immediate-death mode)", worker_id);
    grace_deadlines_.erase(worker_id);
    // RunSummary：记判死时刻（合成时该 worker 最后样本不再计入，复活样本
    // epoch > 此时刻自然重新生效）。
    if (run_metrics_) run_metrics_->on_worker_dead(worker_id);
    // monitor 落盘：DEAD 事件。
    if (metrics_db_) metrics_db_->record_worker_event(worker_id, "DEAD");

    // 判死联动收敛 pending RPC 期待（先于 task 恢复：无限等待方尽早收到
    // 显式失败信号，不依赖后续任何路径）。
    settle_pending_for_dead_worker(worker_id);

    CMVector<uint64_t> tasks_to_recover;
    {
        std::lock_guard<std::mutex> lk(schedule_mutex_);
        worker_manager_->update_worker_status(worker_id, WorkerStatus::DEAD);
        tasks_to_recover = metadata_->get_task_ids_by_worker(worker_id);
    }
    // registry 标死（锁外，storage 锁不与 schedule_mutex_ 嵌套）：读侧副本遍历
    // 将死 holder 排尾，避免 TIER2/TIER3 每次白费一次 connect 超时。remote_idx
    // 条目保持不变（权威视图不因判死丢位置——revive 语义）。
    DataService::instance()->set_worker_alive(worker_id, false);

    // 崩溃恢复：worker 断连可能意味着进程崩溃（收不到失败消息），必须按 task_id
    // 清掉这些 task 声明的 pending frozen，否则该 db 会被永久标"冻结中" → 后续所有
    // 写被拒 → 死锁级 bug。这是 Q1 选 task_id 而非 db_path 的核心理由。
    for (uint64_t task_id : tasks_to_recover) {
        rollback_pending_frozen(task_id);
    }

    if (draining_.load()) {
        // During shutdown: mark running tasks as FAILED so drain can complete.
        // fail_task + remove_task 原子（同 on_task_complete 的保护）。
        for (uint64_t task_id : tasks_to_recover) {
            std::lock_guard<std::mutex> lk(schedule_mutex_);
            metadata_->fail_task(task_id, "Worker disconnected during shutdown");
            graph_->remove_task(task_id);
            record_task_snapshot(task_id);
            WARN("Task failed due to shutdown disconnect: task_id={}", task_id);
        }
        notify_drain_if_active();  // Wake up stop() drain wait.
    } else {
        // 用户确认语义：宽限耗尽 worker 未重连 → WARN 级 user message 附手动
        // 重启命令（worker 侧重连上限与本宽限对等、同窗口自行退出，两侧
        // 收敛；手动重启的 worker 以全新进程从零初始化连接 master）。
        CMString dead_hostname = worker_manager_->get_hostname(worker_id);
        MSG("AGENT::0006", 2,
            "worker {} (host {}) failed to reconnect within grace — declared dead, "
            "its tasks were re-queued. Restart it manually with:\n"
            "  fly --worker --worker-id {} --master-host {} --master-port {} --log-dir {}{}",
            worker_id, dead_hostname, worker_id, host_, port_,
            Config::instance()->get_str("log_dir"),
            dead_hostname.empty() ? "" : (" --host " + dead_hostname));

        // Normal operation: re-queue tasks for recovery.
        // remove_task + add_task + unassign_task 原子：三者分属 graph/metadata 两个
        // 独立锁结构，若与 submit_task/on_task_complete 交错会导致状态分叉。
        for (uint64_t task_id : tasks_to_recover) {
            auto task_opt4 = metadata_->get_task(task_id);
            if (!task_opt4) continue;

            const auto& s = task_opt4->submission_;
            TaskRequirements reqs;
            reqs.capabilities_ = s.required_capabilities_;
            reqs.timeout_seconds_ = s.attribute_timeout_;
            reqs.priority_ = s.priority_;
            {
                std::lock_guard<std::mutex> lk(schedule_mutex_);
                graph_->remove_task(task_id);
                graph_->add_task(task_id, s.inputs_, reqs);
                metadata_->unassign_task(task_id);
            }
            // monitor 落盘：REQUEUE 事件 + 重排队后的 PENDING 行。
            record_task_snapshot(task_id);
            if (metrics_db_) {
                metrics_db_->record_task_event(task_id, worker_id, "REQUEUE");
            }
            WARN("Recovered task from dead worker: task_id={}, name={}", task_id, s.name_);
        }

        if (!tasks_to_recover.empty()) {
            schedule_tasks();
        }
    }

    // 存储接管（storage_takeover_enabled，用户确认语义：同 host storage 节点
    // 只读加载死 worker 的 idx 接管读服务，master 显式驱动，类似重启恢复流程
    // 的运行时版）。发起成功 → 全灭 fail 延迟至 takeover deadline（ack 的
    // mark_data_ready 恢复等待 task）；失败 → 保持现状立即 fail。
    if (try_storage_takeover(worker_id)) {
        // 接管在途：跳过即时 fail，deadline 兜底见 check_takeover_deadlines。
        return;
    }

    fail_orphan_data_objects(worker_id);
}

// 判死联动收敛 pending RPC 期待。数据规模相关等待已无限化（load_db 屏障 /
// merge task / delete ack / merge cleanup 均无超时），无限等待只能被显式
// 失败信号终结——worker 判死即终结其全部期待。幂等：已终结条目（completed
// 已置位 / pending_workers 已 erase / 计数已 clamp）二次判死不改变状态。
void MasterAgent::settle_pending_for_dead_worker(uint64_t worker_id) {
    // 1. IdxLoad 期待：死亡 worker 的加载不会发生，置 -1 让 load_db Python
    //    轮询显式报错（用户侧重试 load_db 会重新 spawn + 重发，天然恢复路径）。
    {
        CMVector<CMString> dbs_to_fail;
        pending_idx_loads_.with_lock([&](auto& m) {
            for (auto& [db_path, p] : m) {
                if (p->pending_workers_.erase(worker_id) > 0) {
                    p->remaining_ = -1;
                    dbs_to_fail.push_back(db_path);
                }
            }
        });
        for (const auto& db : dbs_to_fail) {
            WARN("worker {} died with pending IdxLoad: db_path={} marked failed "
                 "(retry load_db to recover)", worker_id, db);
        }
    }

    // 2. DeleteData 期待：complete(success_=false)——该 host 的源 .dat 删除
    //    未确认，残留数据由 WARN + 等待侧的既有降级路径（STOR::0004 手动
    //    清理提示）暴露。key = "db_path:worker_id"，按尾段精确匹配。
    {
        CMString wid_suffix = ":" + std::to_string(worker_id);
        CMVector<CMString> keys;
        pending_delete_acks_.with_lock([&](auto& m) {
            for (const auto& [key, p] : m) {
                if (!p->completed_ &&
                    key.size() > wid_suffix.size() &&
                    key.compare(key.size() - wid_suffix.size(),
                                wid_suffix.size(), wid_suffix) == 0) {
                    keys.push_back(key);
                }
            }
        });
        for (const auto& key : keys) {
            pending_delete_acks_.complete(key, [worker_id](PendingDeleteData& p) {
                if (!p.completed_) {
                    p.completed_ = true;
                    p.success_ = false;
                    p.error_message_ = "source worker declared dead";
                }
            });
            WARN("worker {} died with pending DeleteData: {} marked failed "
                 "(source .dat may remain on its host)", worker_id, key);
        }
    }

    // 3. MergeTask 期待：complete 失败（等价 on_merge_task_failed 语义——
    //    wait_merge_tasks_complete 收到 success_=false，merge_db 走失败清理
    //    路径，源数据保留支撑重 merge）。
    {
        CMVector<uint64_t> tids;
        merge_task_states_.with_lock([&](auto& m) {
            for (const auto& [tid, s] : m) {
                if (!s->completed_ && s->worker_id_ == worker_id) {
                    tids.push_back(tid);
                }
            }
        });
        for (uint64_t tid : tids) {
            merge_task_states_.complete(tid, [worker_id](MergeTaskState& s) {
                if (!s.completed_) {
                    s.completed_ = true;
                    s.success_ = false;
                    s.error_message_ = "merge worker died: " + std::to_string(worker_id);
                }
            });
            WARN("worker {} died with pending merge task {}: marked failed "
                 "(source preserved for re-merge)", worker_id, tid);
        }
    }

    // 4. MergeCleanup 屏障：死亡 worker 的进程内存索引随进程消失，天然视为
    //    "已清理"——received++（clamp at expected）推进屏障，不阻塞其它
    //    worker 的正常 ack 收敛。
    pending_merge_cleanups_.complete_all_if(
        [](const PendingMergeCleanup&) { return true; },
        [worker_id](PendingMergeCleanup& p) {
            if (p.received_count_ < p.expected_count_) {
                ++p.received_count_;
                WARN("worker {} died during merge cleanup barrier: counted as "
                     "cleaned (progress {}/{})", worker_id,
                     p.received_count_, p.expected_count_);
            }
        });
}

void MasterAgent::fail_orphan_data_objects(uint64_t worker_id) {
    // 数据全灭快速失败：本次判死后，W 持有的对象中"全部 holder 均已 DEAD"的
    //（单副本独占或多副本全灭），把依赖它们的等待调度 task 直接标失败——
    // 避免每个 task 执行期逐个走死连接退避后才失败。运行中 task 不打断
    //（自然读失败上报）。宽限中的其它 holder 不算失效（可能重连恢复）。
    // 幂等可重入：接管超时兜底重跑时，已恢复 holder 的对象不再判 lost。
    CMVector<CMString> held_objects = DataService::instance()->get_objects_of_worker(worker_id);
    CMVector<CMString> lost_objects;
    for (const auto& full : held_objects) {
        bool any_alive = false;
        for (uint64_t holder : DataService::instance()->get_remote_workers(full)) {
            if (holder == worker_id) continue;
            auto info = worker_manager_->get_worker(holder);
            if (info.has_value() && worker_status_alive(info->get().status_)) {
                any_alive = true;
                break;
            }
        }
        if (!any_alive) {
            lost_objects.push_back(full);
        }
    }
    if (!lost_objects.empty()) {
        CMUnorderedSet<CMString> lost_set(lost_objects.begin(), lost_objects.end());
        // 撤销全灭对象的 ready 状态并把依赖 task 从 ready 拉回 pending
        //（data_ready_status_ 不撤销的话，依赖 task 永远被认为可调度，
        // 全是死 holder 的数据只会让执行期逐个读失败）。
        for (const auto& full : lost_objects) {
            graph_->mark_data_removed(full);
        }
        // 只 fail 等待调度（pending）的依赖 task；运行中的不打断（自然读失败上报）。
        CMVector<uint64_t> running_ids = metadata_->get_task_ids_by_status(TaskStatus::RUNNING);
        CMUnorderedSet<uint64_t> running_now(running_ids.begin(), running_ids.end());
        for (uint64_t tid : graph_->get_pending_tasks()) {
            if (running_now.count(tid)) continue;   // 已在执行：不打断
            bool depends_on_lost = false;
            for (const auto& dep : graph_->get_task_dependencies(tid)) {
                if (lost_set.count(dep)) { depends_on_lost = true; break; }
            }
            if (!depends_on_lost) continue;
            {
                std::lock_guard<std::mutex> lk(schedule_mutex_);
                metadata_->fail_task(tid, "data lost: all replicas unavailable (holder worker dead)");
                graph_->remove_task(tid);
            }
            ERR("Task failed (data lost): task_id={} depends on object(s) whose only "
                "holder (worker {}) is dead", tid, worker_id);
        }
        MSG("AGENT::0003", 2,
            "worker {} dead: {} object(s) lost all replicas, dependent waiting tasks failed",
            worker_id, lost_objects.size());
        // 正常收尾（drain 期 master 主动停 worker）触发的对象"全灭"是预期
        // 流程而非故障——不记 ORPHAN_FAIL 事件（避免事件流在每次 run 结束
        // 都出现一串误导读数的红色告警）。运行期判死才落事件。
        if (metrics_db_ && !draining_.load()) {
            metrics_db_->record_event("storage", "ORPHAN_FAIL", worker_id, 0,
                                      CMString("lost=") + std::to_string(lost_objects.size()));
        }
    }
}

bool MasterAgent::try_storage_takeover(uint64_t worker_id) {
    if (Config::instance()->get_int("storage_takeover_enabled") == 0) return false;

    CMString hostname = worker_manager_->get_hostname(worker_id);
    if (hostname.empty()) return false;

    // 该 host 的全部 (db_path, writer_id)——recorded_workers_ 是 _DB_META 的
    // 内存镜像（按 hostname 锚定；worker_id 每次重启变化，不可作键）。
    CMUnorderedMap<CMString, CMVector<CMString>> writers_by_db;
    recorded_workers_.for_each([&](const auto& rec) {
        const auto& [rec_db, rec_host, rec_writer] = rec;
        if (rec_host == hostname) {
            writers_by_db[rec_db].push_back(rec_writer);
        }
    });
    if (writers_by_db.empty()) return false;

    // 同 host 存活 storage_only（选已接管数最轻的）。
    uint64_t storage_wid = 0;
    int64_t min_load = INT64_MAX;
    for (const auto& info : worker_manager_->get_all_workers()) {
        if (info.role_ != WorkerRole::STORAGE_ONLY) continue;
        if (!worker_status_alive(info.status_)) continue;
        if (info.hostname_ != hostname) continue;
        int64_t load = takeover_load_.find(info.worker_id_).value_or(0);
        if (load < min_load) {
            min_load = load;
            storage_wid = info.worker_id_;
        }
    }
    if (storage_wid == 0) {
        DBG("storage takeover: no alive storage_only worker on host={} for dead worker {}",
            hostname, worker_id);
        return false;
    }

    // writer 数上限保护（防同 host 多 worker 连挂涌向单一 storage 导致 OOM）。
    int64_t max_writers = Config::instance()->get_int("storage_takeover_max_writers");
    int64_t total_writers = 0;
    for (const auto& [db, ws] : writers_by_db) {
        total_writers += static_cast<int64_t>(ws.size());
    }
    int64_t cur_load = takeover_load_.find(storage_wid).value_or(0);
    if (max_writers > 0 && cur_load + total_writers > max_writers) {
        WARN("storage takeover: worker {} would exceed max_writers ({}+{} > {}) — "
             "skip takeover, falling back to immediate fail",
             storage_wid, cur_load, total_writers, max_writers);
        return false;
    }

    // 安全红线：storage 只读 restore_entries 加载死 writer 的 idx，绝不以其
    // writer_id 写。链路复用 IdxLoad：ack → rebuild_remote_idx_for_worker →
    // update_remote_idx + mark_data_ready，等待 task 自动恢复调度。
    for (const auto& [db_path, writer_ids] : writers_by_db) {
        send_idx_load_to_worker(db_path, writer_ids, storage_wid);
    }
    takeover_load_.update(storage_wid, [&](int64_t& v) { v += total_writers; });

    int64_t fail_timeout = Config::instance()->get_int("storage_takeover_fail_timeout");
    if (fail_timeout <= 0) fail_timeout = 60;
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    takeover_pending_.update(worker_id, [&](int64_t& t) { t = now + fail_timeout; });

    MSG("AGENT::0004", 1,
        "worker {} dead: storage worker {} on host {} taking over {} writer(s) from {} db(s)",
        worker_id, storage_wid, hostname, total_writers, writers_by_db.size());
    INFO("storage takeover initiated: dead worker={}, storage worker={}, host={}, "
         "dbs={}, writers={} (fail deadline in {}s)",
         worker_id, storage_wid, hostname, writers_by_db.size(), total_writers, fail_timeout);
    if (metrics_db_) {
        metrics_db_->record_event("storage", "STORAGE_TAKEOVER", storage_wid, 0,
                                  CMString("dead_worker=") + std::to_string(worker_id));
    }
    return true;
}

void MasterAgent::check_takeover_deadlines(int64_t now) {
    CMVector<uint64_t> expired;
    takeover_pending_.iterate([&](const uint64_t& dead_wid, const int64_t& deadline) {
        if (now >= deadline) expired.push_back(dead_wid);
    });
    for (uint64_t dead_wid : expired) {
        takeover_pending_.erase(dead_wid);
        // 幂等重判：接管已完成（ack 到达、holder 已更新）则对象有活 holder，
        // fail_orphan 无动作；未完成（idx 丢失/storage 挂了）补做全灭 fail。
        INFO("storage takeover deadline reached for dead worker {} — re-evaluating "
             "orphan data", dead_wid);
        fail_orphan_data_objects(dead_wid);
    }
}

void MasterAgent::check_storage_nodes(int64_t now) {
    if (Config::instance()->get_int("auto_storage_nodes_enabled") == 0) return;

    int64_t interval = Config::instance()->get_int("auto_storage_check_interval");
    if (interval <= 0) interval = 30;
    if (now - last_storage_check_ts_.load() < interval) return;
    last_storage_check_ts_.store(now);

    // 快照分组：host 有无活 storage、活 hybrid 触发者（host 全灭的机器无
    // 触达者，天然不在集合——其恢复走外部调度）。
    CMUnorderedSet<CMString> hosts_with_storage;
    CMUnorderedMap<CMString, uint64_t> hybrid_by_host;
    for (const auto& info : worker_manager_->get_all_workers()) {
        if (!worker_status_alive(info.status_)) continue;
        if (info.role_ == WorkerRole::STORAGE_ONLY) {
            hosts_with_storage.insert(info.hostname_);
        } else {
            hybrid_by_host[info.hostname_] = info.worker_id_;
        }
    }

    // storage 已上线的 host：清残留占位/失败计数（注册路径通常已清，此处
    // 兜底外部唤起——用户 launcher 起的 storage 同样视为覆盖）。
    for (const auto& host : hosts_with_storage) {
        pending_storage_spawns_.erase(host);
        storage_spawn_failures_.erase(host);
    }

    // 占位超时：spawn 是本地秒级动作，占位远超时限仍未注册视为失败，
    // 清除允许重试（受失败退避约束）。
    constexpr int64_t kSpawnPlaceholderTimeoutSec = 120;
    CMVector<CMString> expired_placeholders;
    pending_storage_spawns_.iterate([&](const CMString& host, const int64_t& deadline) {
        if (now >= deadline) expired_placeholders.push_back(host);
    });
    for (const auto& host : expired_placeholders) {
        pending_storage_spawns_.erase(host);
        WARN("auto storage spawn placeholder for host={} expired without "
             "registration — allowing retry", host);
    }

    constexpr int64_t kMaxSpawnFailures = 3;
    for (const auto& [host, trigger_wid] : hybrid_by_host) {
        if (hosts_with_storage.count(host)) continue;
        if (pending_storage_spawns_.contains(host)) continue;  // 在途
        if (storage_spawn_failures_.find(host).value_or(0) >= kMaxSpawnFailures) continue;

        storage_spawn_decisions_.fetch_add(1);
        uint64_t spawn_worker_id = next_auto_spawn_worker_id_.fetch_add(1);
        if (send_storage_spawn_to_worker(trigger_wid, spawn_worker_id)) {
            pending_storage_spawns_.update(host, [&](int64_t& t) {
                t = now + kSpawnPlaceholderTimeoutSec;
            });
            MSG("AGENT::0005", 1,
                "auto storage spawn: host {} has no storage worker — asked worker {} "
                "to spawn one (worker_id={})", host, trigger_wid, spawn_worker_id);
        }
    }
}

void MasterAgent::reject_deferred_register(uint64_t worker_id, const char* reason) {
    auto dr = deferred_registers_.take(worker_id);
    if (!dr) return;
    RegisterAckMessage dup_ack;
    dup_ack.worker_id_ = worker_id;
    dup_ack.master_address_ = host_;
    dup_ack.master_port_ = static_cast<int32_t>(port_);
    dup_ack.duplicate_ = true;
    reactor_->send(dr->conn_id_, dup_ack);
    WARN("Deferred duplicate register rejected (worker_id={}, conn={}): {}",
         worker_id, dr->conn_id_, reason);
}

void MasterAgent::replay_deferred_register(uint64_t worker_id) {
    auto dr = deferred_registers_.take(worker_id);
    if (!dr) return;
    // 旧连接已断（连接表已清）：重放后到者的注册——此刻走正常注册路径
    //（疑似分支不会命中），ack 直达。首连无重发机制，闭环由 master 完成。
    INFO("Replaying deferred register: worker_id={}, conn={} (old connection gone)",
         worker_id, dr->conn_id_);
    on_worker_register(dr->conn_id_, dr->msg_);
}

void MasterAgent::check_dup_register_deadlines(int64_t now) {
    CMVector<uint64_t> expired;
    deferred_registers_.iterate([&](const uint64_t& wid, const DeferredRegister& dr) {
        if (now >= dr.deadline_) expired.push_back(wid);
    });
    for (uint64_t wid : expired) {
        // 探测无结论（旧连接半死不活）→ 保守拒绝（先到先得）。
        reject_deferred_register(wid, "probe inconclusive within deadline");
    }
}

void MasterAgent::on_storage_spawn_ack(uint64_t conn_id, const StorageSpawnAckMessage& msg) {
    if (msg.success_) {
        // exec 成功：保留占位等注册到达（on_worker_register 清除）。
        INFO("auto storage spawn ack: host={}, spawned by worker {} — awaiting "
             "registration", msg.hostname_, msg.worker_id_);
        return;
    }
    // exec 失败：清占位允许下轮重试，失败计数累积（3 次放弃该 host）。
    pending_storage_spawns_.erase(msg.hostname_);
    int64_t fails = 0;
    storage_spawn_failures_.update(msg.hostname_, [&](int64_t& v) { fails = ++v; });
    WARN("auto storage spawn failed on host={} (by worker {}): {} — failure #{}, "
         "giving up this host after 3",
         msg.hostname_, msg.worker_id_, msg.error_message_, fails);
}

bool MasterAgent::send_storage_spawn_to_worker(uint64_t worker_id, uint64_t spawn_worker_id) {
    StorageSpawnRequestMessage msg;
    msg.spawn_worker_id_ = spawn_worker_id;

    uint64_t conn = lookup_worker_conn(worker_id);
    if (conn == 0) {
        ERR("send_storage_spawn_to_worker: worker_id={} not found", worker_id);
        return false;
    }
    reactor_->send(conn, msg);
    return true;
}

void MasterAgent::on_error(uint64_t conn_id, int error_code) {
    ERR("Connection error: conn_id={}, error={}", conn_id, error_code);
    on_disconnect(conn_id);
}

CMVector<uint64_t> MasterAgent::get_connected_workers() const {
    CMVector<uint64_t> workers;
    std::lock_guard<std::mutex> lk(workers_mutex_);
    for (const auto& [conn, worker] : conn_to_worker_) {
        workers.push_back(worker);
    }
    return workers;
}

CMVector<std::pair<uint64_t, uint64_t>> MasterAgent::snapshot_worker_conns() const {
    std::lock_guard<std::mutex> lk(workers_mutex_);
    CMVector<std::pair<uint64_t, uint64_t>> conns;
    conns.reserve(worker_to_conn_.size());
    for (const auto& [wid, cid] : worker_to_conn_) {
        conns.emplace_back(wid, cid);
    }
    return conns;
}

uint64_t MasterAgent::lookup_worker_conn(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lk(workers_mutex_);
    auto it = worker_to_conn_.find(worker_id);
    return it == worker_to_conn_.end() ? 0 : it->second;
}

CMVector<std::pair<uint64_t, CMString>> MasterAgent::get_worker_hostnames() const {
    CMVector<std::pair<uint64_t, CMString>> result;
    // 遍历 WorkerManager（hostname 已收编进 WorkerInfo），取代原并行 map。
    for (const auto& info : worker_manager_->get_all_workers()) {
        result.push_back({info.worker_id_, info.hostname_});
    }
    return result;
}

CMVector<uint64_t> MasterAgent::get_storage_only_workers() const {
    CMVector<uint64_t> result;
    // Python 侧（load_db 同 host 加载目标选择等）识别 storage 节点用。
    for (const auto& info : worker_manager_->get_all_workers()) {
        if (info.role_ == WorkerRole::STORAGE_ONLY) {
            result.push_back(info.worker_id_);
        }
    }
    return result;
}

void MasterAgent::add_worker_hostname(uint64_t worker_id, const CMString& hostname,
                                      WorkerRole role) {
    // 转发到 WorkerManager（hostname 收编进 WorkerInfo）。若 worker 未注册则先注册，
    // 兼容测试在无网络注册流程下直接设置拓扑的场景（role 一并注入，供
    // select_backup_worker 的 storage 偏好用例构造混合拓扑）。
    if (!worker_manager_->get_worker(worker_id)) {
        worker_manager_->register_worker(worker_id, "", 0, {}, "", "", role);
    }
    worker_manager_->set_hostname(worker_id, hostname);
}

size_t MasterAgent::get_connection_count() const {
    std::lock_guard<std::mutex> lk(workers_mutex_);
    return conn_to_worker_.size();
}

void MasterAgent::expect_worker(uint64_t worker_id) {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // update（覆盖时间戳）：重复 expect 视为重新唤起，超时窗口从最后一次算起。
    expected_worker_ids_.update(worker_id, [&](int64_t& t) { t = now; });
    DBG("expected worker registered as placeholder: worker_id={}", worker_id);
}

size_t MasterAgent::get_expected_worker_count() const {
    return expected_worker_ids_.size();
}

std::pair<CMVector<std::pair<uint64_t, CMVector<CMString>>>, size_t>
MasterAgent::snapshot_worker_pool() {
    // 原子采样「在册 hybrid 池 + 未注册占位符数」：持 expected 锁单点完成，
    // 与 on_worker_register 的持锁转正段互斥——过渡态（已离开占位表、尚未
    // 进 WorkerManager）对采样不可见，容量口径无瞬时漏计。锁序
    // expected → manager 与注册路径同向，无死锁环。
    // 每个 worker 一条目（capabilities 允许为空）——空属性 worker 不丢。
    CMVector<std::pair<uint64_t, CMVector<CMString>>> pool;
    size_t pending = 0;
    expected_worker_ids_.with_lock([&](auto& m) {
        pending = m.size();
        auto fill = [&](const CMVector<uint64_t>& ids) {
            for (auto wid : ids) {
                pool.emplace_back(wid, worker_manager_->get_worker_capabilities(wid));
            }
        };
        fill(worker_manager_->get_idle_workers());
        fill(worker_manager_->get_busy_workers());
    });
    return {pool, pending};
}

bool MasterAgent::all_workers_registered() const {
    return expected_worker_ids_.size() == 0;
}

void MasterAgent::check_expected_worker_timeouts(int64_t now) {
    // 0 = 不假设注册时限（默认；bsub 慢调度场景不清理）。
    int64_t timeout = Config::instance()->get_int("worker_register_timeout");
    if (timeout <= 0) return;
    expected_worker_ids_.with_lock([&](auto& m) {
        for (auto it = m.begin(); it != m.end(); ) {
            if (now - it->second > timeout) {
                WARN("worker {} did not register within {}s of launch "
                     "(expected-worker placeholder expired, giving up)",
                     it->first, timeout);
                it = m.erase(it);
            } else {
                ++it;
            }
        }
    });
}

void MasterAgent::check_grace_deadlines(int64_t now) {
    // 宽限表通常为空（worker 在线时无登记）——with_lock 遍历仅在有断连时发生。
    CMVector<uint64_t> expired;
    grace_deadlines_.with_lock([&](const auto& m) {
        for (const auto& [worker_id, deadline] : m) {
            if (now >= deadline) {
                expired.push_back(worker_id);
            }
        }
    });
    if (!expired.empty()) {
        INFO("check_grace_deadlines: {} worker(s) expired at now={} — declaring dead",
             expired.size(), now);
    }
    for (uint64_t worker_id : expired) {
        // handle_worker_death 内部先 erase 宽限条目再处理，幂等。
        handle_worker_death(worker_id);
    }
}

CMVector<uint64_t> MasterAgent::get_pending_tasks() const {
    return graph_->get_pending_tasks();
}

CMVector<uint64_t> MasterAgent::get_running_tasks() const {
    return metadata_->get_task_ids_by_status(TaskStatus::RUNNING);
}

CMVector<uint64_t> MasterAgent::get_completed_tasks() const {
    auto result = metadata_->get_task_ids_by_status(TaskStatus::COMPLETED);
    // 诊断（WARN=flush）：当 Python 读取 completed_tasks 时，对比 metadata 与 graph
    // 两套来源的已完成 task 数。若不一致 → 找到了"读不到"的根因。
    size_t graph_count = graph_->completed_count();
    if (result.size() != graph_count) {
        WARN("[COMPLETED-MISMATCH] metadata_completed={} graph_completed={}",
             result.size(), graph_count);
    }
    return result;
}

CMVector<uint64_t> MasterAgent::get_failed_tasks() const {
    return metadata_->get_task_ids_by_status(TaskStatus::FAILED);
}

CMString MasterAgent::get_task_error(uint64_t task_id) const {
    auto task_opt5 = metadata_->get_task(task_id);
    if (task_opt5) {
        return task_opt5->error_message_;
    }
    return "";
}

void MasterAgent::register_database(const CMString& db_path, const CMString& data_path) {
    INFO("register_database: db_path={}, data_path={}", db_path, data_path);
    // db_path 本该唯一：重复 register 会丢弃旧 Database 的状态（var_store_、writer_id、
    // 冻结状态、对象状态）并覆盖 DataService::db_paths_。正常流程每个 db_path 只注册一次；
    // merge 重建走独立路径（见 cleanup_after_merge）。命中此 WARN = 重复注册 bug。
    // Database 构造是重 IO（目录/_DB_META/_VARS/DataWriter），在容器锁外执行；
    // 锁内二次检查 + 插入（D4 拆除——防写锁长时间阻塞高频读点）。
    auto db = CMMakeShared<Database>(db_path, data_path, 0, "", db_path);
    // 覆盖插入时旧实例若仅容器持有，~Database（WBQ drain 重活）会在锁内跑——
    // displaced 把旧实例带出锁外析构（§13.3）。
    CMSharedPtr<Database> displaced;
    {
        std::unique_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto it = db_instances_.find(db_path);
        if (it != db_instances_.end()) {
            displaced = std::move(it->second);
            WARN("[DB-DUP] register_database: db_path={} already exists — overwriting (possible bug)", db_path);
        }
        db_instances_[db_path] = db;
    }
    // RunSummary：master 首见 db（幂等，首见语义不覆盖；merge 路径变更走
    // record_db_paths_changed）。锁外登记（collector 自身锁，快速路径）。
    if (run_metrics_) run_metrics_->record_db_created(db_path, data_path);
    if (metrics_db_) metrics_db_->record_event("db", "DB_CREATED", 0, 0, db_path);
    registered_dbs_.insert(db_path);
}

bool MasterAgent::is_db_frozen(const CMString& db_path) const {
    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        if (frozen_dbs_.count(db_path) > 0 || pending_frozen_dbs_.count(db_path) > 0) {
            return true;
        }
    }
    // 本进程内存未登记（如迁移场景的新 master 进程）：以磁盘 _FROZEN marker
    // 为权威证据（freeze 时 create_frozen_marker 落盘）。跨进程语义完整——
    // migrate_project(consolidate=True) 的前置校验依赖它。
    return std::filesystem::exists(std::string(db_path) + "/_FROZEN");
}

bool MasterAgent::is_db_pending_frozen(const CMString& db_path) const {
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    return pending_frozen_dbs_.count(db_path) > 0;
}

void MasterAgent::commit_pending_frozen(uint64_t task_id) {
    // task 成功：该 task 的 pending 项迁移到 confirmed + 广播给所有 worker。
    CMVector<CMString> committed;
    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        for (auto it = pending_frozen_dbs_.begin(); it != pending_frozen_dbs_.end(); ) {
            if (it->second == task_id) {
                if (frozen_dbs_.insert(it->first).second) {
                    committed.push_back(it->first);   // 仅广播新增的 confirmed
                }
                it = pending_frozen_dbs_.erase(it);
            } else {
                ++it;
            }
        }
    }
    // 本地 freeze + 广播（task 成功后才广播）
    for (const auto& db_path : committed) {
        // 锁内只 find + 拷 shared_ptr：freeze（drain/marker/vars 落盘，重 IO）、
        // provenance 清理、workers 广播全部移出容器锁（D2 拆除——原读锁延伸
        // 覆盖 provenance_mutex_ + workers_mutex_ + reactor send 嵌套链）。
        CMSharedPtr<Database> db;
        {
            std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
            auto it = db_instances_.find(db_path);
            if (it != db_instances_.end()) db = it->second;
        }
        if (db) db->freeze();
        // Part C: freeze 确认后 provenance 已失效，立即清理释放内存。
        cleanup_provenance_for_db(db_path);
        // RunSummary：封闭 db 窗口 + du 统计磁盘终值（锁外，ms 级）。
        if (run_metrics_) run_metrics_->record_db_frozen(db_path);
        if (metrics_db_) {
            metrics_db_->record_event("db", "DB_FROZEN", 0, task_id, db_path);
            record_db_du(db_path);  // freeze 后 du 即终值（RunMetrics 已同步测得）
        }
        INFO("DB frozen (committed by task): db_path={}, task_id={}", db_path, task_id);
        DatabaseFreezeNotification broadcast_msg;
        broadcast_msg.db_path_ = db_path;
        for (const auto& [wid, cid] : snapshot_worker_conns()) {
            (void)wid;
            reactor_->send(cid, broadcast_msg);
        }
        // 流程 message：freeze 完成（不可逆里程碑）。
        MSG("STOR::0001", 1, "db {} frozen (committed by task {})", db_path, task_id);
    }
}

void MasterAgent::rollback_pending_frozen(uint64_t task_id) {
    // task 失败/崩溃：按 task_id 清除 pending（worker 本地 reset 由失败处理流程负责）。
    // 这是覆盖崩溃失败的关键：master 收不到失败消息，只能靠 task_id 反查清理。
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    for (auto it = pending_frozen_dbs_.begin(); it != pending_frozen_dbs_.end(); ) {
        if (it->second == task_id) {
            WARN("Rolling back pending freeze: db_path={}, task_id={}", it->first, task_id);
            it = pending_frozen_dbs_.erase(it);
        } else {
            ++it;
        }
    }
}

CMSharedPtr<Database> MasterAgent::get_or_create_database(const CMString& db_path, const CMString& data_path, uint64_t writer_id) {
    // Database 是 master 进程 DB 路径的【唯一权威源】：路径内嵌于对象，DataService::db_paths_
    // 由 Database 构造时自动 register，无需手动双写第二份字符串副本。
    //
    // 方法名暗示"get or create"，但当前实现总是 create+覆盖。重复调用同 db_path 会丢弃
    // 旧 Database 状态。命中此 WARN 说明调用方本该用 get_database 复用却误入了创建路径。
    // 构造重 IO 在容器锁外；锁内二次检查 + 插入（D4 拆除）。
    auto db = CMMakeShared<Database>(db_path, data_path, writer_id);
    // displaced 同 register_database：旧实例带出锁外析构（§13.3）。
    CMSharedPtr<Database> displaced;
    {
        std::unique_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto it = db_instances_.find(db_path);
        if (it != db_instances_.end()) {
            displaced = std::move(it->second);
            WARN("[DB-DUP] get_or_create_database: db_path={} already exists — recreating (possible bug, should reuse)", db_path);
        }
        db_instances_[db_path] = db;
    }
    // RunSummary：master 首见 db（幂等；与 register_database 覆盖 load/自写
    // 两类创建入口）。
    if (run_metrics_) run_metrics_->record_db_created(db_path, data_path);
    if (metrics_db_) metrics_db_->record_event("db", "DB_CREATED", 0, 0, db_path);
    registered_dbs_.insert(db_path);
    return db;
}

CMSharedPtr<Database> MasterAgent::get_database(const CMString& db_path) const {
    std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
    auto it = db_instances_.find(db_path);
    return it != db_instances_.end() ? it->second : nullptr;
}

CMVector<uint64_t> MasterAgent::get_idle_workers() const {
    return worker_manager_->get_idle_workers();
}

bool MasterAgent::assign_worker_attributes(uint64_t worker_id, const CMVector<CMString>& added_properties) {
    if (added_properties.empty()) return true;

    WorkerPropertyAssignMessage msg;
    msg.worker_id_ = worker_id;
    msg.added_properties_ = added_properties;

    uint64_t conn = lookup_worker_conn(worker_id);
    if (conn == 0) {
        ERR("assign_worker_attributes: worker_id={} not connected", worker_id);
        return false;
    }
    reactor_->send(conn, msg);
    INFO("WorkerPropertyAssign sent: worker_id={}, count={}", worker_id, added_properties.size());
    return true;
}

size_t MasterAgent::count_workers_with_all_capabilities(const CMVector<CMString>& capabilities) const {
    return worker_manager_->count_workers_with_all_capabilities(capabilities);
}

CMVector<uint64_t> MasterAgent::get_busy_workers() {
    return worker_manager_->get_busy_workers();
}

CMVector<CMString> MasterAgent::get_worker_capabilities(uint64_t worker_id) {
    return worker_manager_->get_worker_capabilities(worker_id);
}

void MasterAgent::on_data_query_dispatch(uint64_t conn_id, const DataQueryMessage& msg) {
    INFO("DataQuery for object: {}", msg.object_name_);

    DataService::instance();
    DataLocationMessage response;
    response.object_name_ = msg.object_name_;

    if (DataService::instance()->has_remote_location(msg.object_name_)) {
        auto all_locs = DataService::instance()->lookup_all_remote_idx(msg.object_name_);
        DBG("[TEMP-QUERY] DataQuery FOUND: obj={}, replicas={}", msg.object_name_, all_locs.size());
        response.success_ = true;
        response.can_still_produce_ = false;
        for (const auto& loc : all_locs) {
            DataLocation dl;
            dl.object_name = msg.object_name_;
            dl.worker_id = loc.worker_id_;
            dl.host = loc.host_;
            dl.port = loc.port_;
            dl.storage_only = loc.storage_only_ ? 1 : 0;
            response.locations_.push_back(std::move(dl));
        }

        // master 不再自行统计读流量判定 backup（master 有盲区：worker 缓存对象位置后不再查
        // master，越热的对象 master 反而统计不到）。改由 worker 主动观测 TIER2 读流量并上报
        // suggest（WorkerBackupSuggestMessage），master EWMA 聚合后判定（on_worker_backup_suggest）。
    } else {
        response.success_ = false;
        bool has_pending = !graph_->get_pending_tasks().empty();
        bool has_running = metadata_->has_tasks_with_status(TaskStatus::RUNNING);
        response.can_still_produce_ = has_pending || has_running;
        DBG("[TEMP-QUERY] DataQuery NOT FOUND: obj={}, can_still_produce={}", msg.object_name_, response.can_still_produce_);
    }

    reactor_->send(conn_id, response);
}

// === write_provenance_ 嵌套访问封装 ===
// 外层 key=db_path，内层 key=short_name。所有方法内部持 provenance_mutex_。

bool MasterAgent::provenance_check_and_register(const CMString& db_path, const CMString& short_name,
                                                const CMString& hash, CMString& err_msg) {
    if (hash.empty()) {
        // 上游（commit_write 时间戳 / task context）应保证非空；空到达说明异常路径，
        // 拒绝而非静默放行（消灭空 hash 旁路）。
        err_msg = "Empty write_context_hash for " + db_path + ":" + short_name;
        return false;
    }
    std::lock_guard<std::mutex> lk(provenance_mutex_);
    auto& inner = write_provenance_[db_path];
    auto it = inner.find(short_name);
    if (it == inner.end()) {
        inner[short_name] = hash;
        return true;
    }
    if (it->second == hash) {
        return true;
    }
    err_msg = "Write provenance mismatch for " + db_path + ":" + short_name +
        ": existing hash=" + it->second + " new hash=" + hash;
    return false;
}

void MasterAgent::provenance_erase(const CMString& db_path, const CMString& short_name) {
    std::lock_guard<std::mutex> lk(provenance_mutex_);
    auto outer = write_provenance_.find(db_path);
    if (outer == write_provenance_.end()) return;
    outer->second.erase(short_name);
    if (outer->second.empty()) {
        write_provenance_.erase(outer);  // 清空桶，避免累积空 inner map
    }
}

void MasterAgent::cleanup_provenance_for_db(const CMString& db_path) {
    std::lock_guard<std::mutex> lk(provenance_mutex_);
    write_provenance_.erase(db_path);  // freeze 时整体释放
}

size_t MasterAgent::provenance_count_for_testing(const CMString& db_path) const {
    std::lock_guard<std::mutex> lk(provenance_mutex_);
    auto it = write_provenance_.find(db_path);
    return it == write_provenance_.end() ? 0 : it->second.size();
}

CMString MasterAgent::provenance_lookup(const CMString& db_path, const CMString& short_name) {
    std::lock_guard<std::mutex> lk(provenance_mutex_);
    auto outer = write_provenance_.find(db_path);
    if (outer == write_provenance_.end()) return {};
    auto inner = outer->second.find(short_name);
    if (inner == outer->second.end()) return {};
    return inner->second;
}

// 纯逻辑：处理 WriteRegister 的全部业务（provenance/mark_data_ready/update_remote_idx 带 size/
// schedule_tasks/recorded_workers_ 登记/auto-backup 评估），返回 ack。调用方决定是否回 ACK。
// worker 路径：on_write_register 调本函数后 reactor_->send(ack)。
// master 自写路径：on_master_register_write 调本函数后丢弃 ack。
WriteRegisterAckMessage MasterAgent::do_write_register(const WriteRegisterMessage& msg) {
    DBG("WriteRegister: worker={}, object={}, db_path={}", msg.worker_id_, msg.object_name_, msg.db_path_);

    WriteRegisterAckMessage ack;
    ack.object_name_ = msg.object_name_;
    ack.db_path_ = msg.db_path_;

    bool registered_ok = false;
    {
        // frozen 检查与 provenance 登记同一临界区（原 TOCTOU：检查与登记之间，
        // 其他连接的 freeze commit + cleanup_provenance_for_db 可插入，已冻结 db
        // 的 provenance 又被登记）。锁序 frozen_dbs_mutex_ → provenance_mutex_，
        // 与 commit_pending_frozen 路径一致，无反向持锁。
        std::lock_guard<std::mutex> flk(frozen_dbs_mutex_);
        bool frozen = frozen_dbs_.count(msg.db_path_) > 0 ||
                      pending_frozen_dbs_.count(msg.db_path_) > 0;
        if (frozen) {
            ack.success_ = false;
            ack.error_message_ = "Database frozen: " + msg.db_path_;
            ack.error_type_ = TaskErrorType::WRITE_TO_FROZEN_DB;
            WARN("WriteRegister rejected: db {} is frozen", msg.db_path_);
            if (metrics_db_) metrics_db_->record_event("storage", "WRITE_REJECTED", msg.worker_id_, 0, "frozen: " + msg.object_name_);
        } else if (msg.write_context_hash_.empty()) {
            // 空 hash 是非法注册请求（上游 commit_write 时间戳 guard / task context
            // 应保证非空），与 provenance mismatch（已有不同 hash 的对比）语义不同，
            // 归类 REGISTRATION_FAILED（注册被拒的通用兜底）。
            ack.success_ = false;
            ack.error_message_ = "Empty write_context_hash for " + msg.object_name_;
            ack.error_type_ = TaskErrorType::WRITE_REGISTRATION_FAILED;
            ERR("WriteRegister rejected: empty write_context_hash for {}", msg.object_name_);
            if (metrics_db_) metrics_db_->record_event("storage", "WRITE_REJECTED", msg.worker_id_, 0, "empty_hash: " + msg.object_name_);
        } else {
            auto [prov_db, prov_short] = fly::split_full_name(msg.object_name_);
            CMString err_msg;
            if (provenance_check_and_register(prov_db, prov_short, msg.write_context_hash_, err_msg)) {
                registered_ok = true;
            } else {
                ack.success_ = false;
                ack.error_message_ = err_msg;
                ack.error_type_ = TaskErrorType::WRITE_PROVENANCE_MISMATCH;
                ERR("WriteRegister rejected: provenance mismatch for {}", msg.object_name_);
                if (metrics_db_) metrics_db_->record_event("storage", "WRITE_REJECTED", msg.worker_id_, 0, "provenance: " + msg.object_name_);
            }
        }
    }

    if (registered_ok) {
        ack.success_ = true;

        // 预许可（§14.1 注册时序）：只做许可+provenance，跳过可见性登记——
        // 流式写的许可探测（不带 size；数据尚未写）。完成登记（非 preliminary）
        // 到达时才激活可见性。
        if (msg.preliminary_) {
            DBG("WriteRegister preliminary accepted: object={}", msg.object_name_);
            return ack;
        }

        // Q2 决策：可见性登记段按模式分流。
        // master 自写（worker_id_==0）强制即时登记 —— master 进程无 task 三阶段
        // （不设 transaction_mode、无 TaskCompleteMessage），没有延迟登记的触发时机。
        // stream 模式（默认）：即时 mark_data_ready + update_remote_idx + schedule。
        // 非 stream 模式：仅 ack 成功，可见性登记延迟到 on_task_complete 的 written_objects_
        //   统一处理（task 级原子性 —— 失败回滚后下游 task 不会被错误调度）。
        bool master_self_write = (msg.worker_id_ == 0);
        bool streaming_mode = master_self_write ||
                             (Config::instance()->get_int("dependency_update_mode") == 0);
        if (streaming_mode) {
            // 顺序约束：位置登记（update_remote_idx/缓存/元数据）全部就绪后
            // 才能 mark_data_ready 唤醒依赖——依赖 task 被唤醒后可能跨线程
            // 立即读（TIER3 经独立连接查询），位置晚于唤醒会导致 NOT FOUND
            //（读方三次快重试全落在 record_worker_info 的磁盘 IO 窗口内，
            // solver n10 高并发实测撞中）。record_worker_info（可能 append
            // _DB_META，慢 IO）也前移，mark_data_ready 保持最后一步。
            auto addr = DataService::instance()->get_worker_address(msg.worker_id_);
            DataService::instance()->update_remote_idx(msg.object_name_, msg.worker_id_, addr.host_, addr.port_, msg.size_bytes_);
            update_dependency_location_cache(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
            record_worker_info(msg.object_name_, msg.db_path_, msg.worker_id_, msg.writer_id_);
            graph_->mark_data_ready(msg.object_name_);
            // master 自写对象（worker_id==0）的 backup 不再由 master 主动评估触发——
            // 新设计下 master 不自统计读流量（有盲区）。master 写入的对象待 worker 跨机读取后，
            // 由 worker 上报 suggest（WorkerBackupSuggestMessage）→ master EWMA 聚合判定 backup。
            schedule_tasks();
        }
        // 非 stream 模式：provenance 已登记（校验段），但 mark_data_ready /
        // update_remote_idx / record_worker_info 延迟到 task 完成时统一处理。
    }

    return ack;
}

void MasterAgent::on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg) {
    auto ack = do_write_register(msg);
    reactor_->send(conn_id, ack);
}

void MasterAgent::on_worker_property_update(uint64_t conn_id, const WorkerPropertyUpdateMessage& msg) {
    size_t added_count = msg.added_properties_.size();
    size_t removed_count = msg.removed_properties_.size();
    INFO("WorkerPropertyUpdate: worker_id={}, added={}, removed={}", msg.worker_id_, added_count, removed_count);

    worker_manager_->update_capabilities(msg.worker_id_, msg.added_properties_, msg.removed_properties_);
    if (metrics_db_ && (added_count > 0 || removed_count > 0)) {
        metrics_db_->record_event("worker", "PROPERTY_UPDATE", msg.worker_id_, 0,
                                  CMString("+") + std::to_string(added_count) + "/-" +
                                  std::to_string(removed_count));
    }

    // 属性变化后立即触发调度：worker 通过 set_worker_property 获得新属性后，
    // 等待该属性的 task（waiting 中）应立即被调度，无需等到 timeout。
    schedule_tasks();
}

void MasterAgent::broadcast_object_removed(const CMString& db_path, const CMString& object_name) {
    CMString full = db_path + ":" + object_name;

    DataService::instance()->remove_remote_index(full);
    provenance_erase(db_path, object_name);

    ObjectRemovedMessage msg;
    msg.object_name_ = full;
    msg.db_path_ = db_path;

    for (const auto& [wid, cid] : snapshot_worker_conns()) {
        (void)wid;
        reactor_->send(cid, msg);
    }
}

void MasterAgent::broadcast_message_limits() {
    // 从 master 的 MessageRegistry 取当前所有配额设置（全量快照），广播给所有在线 worker。
    // worker 收到后整体替换本地配额（不清零计数）。支持运行时动态修改：每次 set_*_limit 触发。
    MessageLimitSyncMessage msg;
    fly::MessageRegistry::instance().get_all_limits(
        msg.global_limit_, msg.domain_keys_, msg.domain_values_,
        msg.id_keys_, msg.id_values_);

    for (const auto& [wid, cid] : snapshot_worker_conns()) {
        (void)wid;
        reactor_->send(cid, msg);
    }
}

void MasterAgent::on_var_set(uint64_t conn_id, const VarSetMessage& msg) {
    VarAckMessage ack;
    ack.var_name_ = msg.var_name_;  // echo the full name

    auto [db_path, short_name] = split_full_name(msg.var_name_);
    // 锁内只 find + 拷 shared_ptr：var 族（var_mutex_ 自保护）与 send/broadcast
    // 全部移出容器锁（D2 拆除：原实现持 db 读锁做 value 拷贝 + broadcast + send）。
    CMSharedPtr<Database> db;
    {
        std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto it = db_path.empty() ? db_instances_.end() : db_instances_.find(db_path);
        if (it != db_instances_.end()) {
            db = it->second;
        }
    }
    if (!db) {
        ack.success_ = false;
        ack.error_message_ = "db not found on master";
        reactor_->send(conn_id, ack);
        return;
    }

    // value_ is mutable: std::move it directly into the FlyBuffer (zero-copy).
    // The decoded msg is a local destroyed right after this handler returns, so
    // moving its payload is safe despite the const& handler contract.
    auto buf = CMMakeShared<FlyBuffer>();
    buf->take(std::move(msg.value_));

    bool ok = db->master_set_var(short_name, buf, msg.type_name_);
    ack.success_ = ok;
    if (!ok) {
        if (db->is_frozen()) {
            ack.error_message_ = "db frozen";
        } else {
            ack.error_message_ = "var already exists (immutable)";
            // Broadcast so workers drop any stale local cache of the rejected var.
            broadcast_var(msg.var_name_, true);
        }
    }
    reactor_->send(conn_id, ack);
}

void MasterAgent::on_var_get(uint64_t conn_id, const VarGetMessage& msg) {
    VarAckMessage ack;
    ack.var_name_ = msg.var_name_;  // echo the full name

    auto [db_path, short_name] = split_full_name(msg.var_name_);
    CMSharedPtr<Database> db;
    {
        std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto it = db_path.empty() ? db_instances_.end() : db_instances_.find(db_path);
        if (it != db_instances_.end()) {
            db = it->second;
        }
    }
    if (!db) {
        ack.success_ = false;
        reactor_->send(conn_id, ack);
        return;
    }

    auto [found, value, type_name] = db->master_get_var(short_name);
    ack.success_ = found;
    if (found && value) {
        // The var_store_ FlyBufferPtr is shared (other readers may hold it), so
        // its contents cannot be moved out — one copy into the message CMString
        // field, which bitsery then serializes onto the wire.
        ack.value_.assign(value->data(), value->size());
        ack.type_name_ = type_name;
    }
    reactor_->send(conn_id, ack);
}

void MasterAgent::on_var_remove(uint64_t conn_id, const VarRemoveMessage& msg) {
    auto [db_path, short_name] = split_full_name(msg.var_name_);
    if (!db_path.empty()) {
        CMSharedPtr<Database> db;
        {
            std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
            auto it = db_instances_.find(db_path);
            if (it != db_instances_.end()) {
                db = it->second;
            }
        }
        if (db) {
            db->master_remove_var(short_name);
            // Broadcast the removal (full name) to all workers so they drop caches.
            broadcast_var(msg.var_name_, false);
        }
    }
}

void MasterAgent::broadcast_var(const CMString& full_var_name, bool is_modification_reject) {
    VarBroadcastMessage msg;
    msg.var_name_ = full_var_name;
    msg.is_modification_reject_ = is_modification_reject;

    for (const auto& [wid, cid] : snapshot_worker_conns()) {
        (void)wid;
        reactor_->send(cid, msg);
    }
}

void MasterAgent::on_master_remove(const CMString& db_path, const CMString& object_name) {
    // master 进程内同步 remove（db.remove_object 经 request_remove_func 触发）。
    // 清 graph/remote_idx/provenance + 通知持有对象的 worker 清 local data。
    CMString full = db_path + ":" + object_name;
    graph_->mark_data_removed(full);

    // master 自身也可能是持有者（master 自写对象，如编排层写入的中间量）：
    // 清本进程的 local index 与 ObjectCache——调用方进程（发起 remove 的
    // worker）只清它自己的缓存，master 侧残留会让 master 的 read_object
    // 仍从 low cache 命中已删对象（temp 对象同理）。对不存在的条目是 no-op。
    DataService::instance()->remove_local_index(full);
    ObjectCache::instance().remove(full);

    auto worker_ids = DataService::instance()->get_remote_workers(full);
    for (auto wid : worker_ids) {
        uint64_t worker_conn_id = 0;
        {
            std::lock_guard<std::mutex> lk(workers_mutex_);
            auto it = worker_to_conn_.find(wid);
            if (it != worker_to_conn_.end()) {
                worker_conn_id = it->second;
            }
        }
        if (worker_conn_id == 0) continue;
        RemoveCommandMessage cmd;
        cmd.db_path_ = db_path;
        cmd.object_name_ = full;
        reactor_->send(worker_conn_id, cmd);
        INFO("RemoveCommand sent to worker_id={}: object={}", wid, full);
    }

    DataService::instance()->remove_remote_location(full);
    provenance_erase(db_path, object_name);
    schedule_tasks();
}

void MasterAgent::on_remove_request(uint64_t conn_id, const RemoveRequestMessage& msg) {
    INFO("RemoveRequest: object={}, db_path={}", msg.object_name_, msg.db_path_);
    auto [db_path, short_name] = fly::split_full_name(msg.object_name_);
    on_master_remove(db_path, short_name);

    RemoveAckMessage ack;
    ack.db_path_ = msg.db_path_;
    ack.object_name_ = msg.object_name_;
    ack.success_ = true;
    reactor_->send(conn_id, ack);

    INFO("RemoveRequest completed: object={}", msg.object_name_);
}

CMString MasterAgent::get_failed_tasks_file_path(const CMString& owner_db_path) const {
    // Task db 归属规则：失败记录按归属 db 落盘 {owner_db_path}/failed_tasks.bin
    // （project 场景 db 目录在 project 下，bin 天然随 db 迁移/持久，支持多 project）；
    // 无归属 task（参数无 db）fallback {log_dir}/failed_tasks.bin。
    if (!owner_db_path.empty()) {
        namespace fs = std::filesystem;
        if (!fs::exists(owner_db_path)) {
            fs::create_directories(owner_db_path);
        }
        return owner_db_path + "/failed_tasks.bin";
    }
    CMString log_dir = Config::instance()->get_str("log_dir");
    namespace fs = std::filesystem;
    if (!fs::exists(log_dir)) {
        fs::create_directories(log_dir);
    }
    return log_dir + "/failed_tasks.bin";
}

namespace {

void append_failed_record(const CMString& file_path, const FailedTaskRecord& record) {
    CMString body;
    FLY_ENCODE(record, body);

    int64_t body_size = static_cast<int64_t>(body.size());

    std::ofstream ofs(file_path, std::ios::binary | std::ios::app);
    if (!ofs.is_open()) {
        ERR("Failed to open failed tasks file: {}", file_path);
        return;
    }
    ofs.write(reinterpret_cast<const char*>(&body_size), sizeof(body_size));
    ofs.write(body.data(), body.size());
    ofs.flush();
}

CMVector<FailedTaskRecord> read_failed_records(const CMString& file_path) {
    CMVector<FailedTaskRecord> result;

    if (!std::filesystem::exists(file_path)) return result;

    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs) return result;

    while (true) {
        int64_t body_size = 0;
        ifs.read(reinterpret_cast<char*>(&body_size), sizeof(body_size));
        if (!ifs || body_size <= 0) break;

        CMString body(body_size, '\0');
        ifs.read(body.data(), body_size);
        if (!ifs) break;

        FailedTaskRecord record;
        try {
            FLY_DECODE(body, FailedTaskRecord, record);
            result.push_back(std::move(record));
        } catch (...) {}
    }

    return result;
}

void rewrite_failed_records(const CMString& file_path, const CMVector<FailedTaskRecord>& records) {
    std::filesystem::remove(file_path);

    if (records.empty()) return;

    for (const auto& r : records) {
        append_failed_record(file_path, r);
    }
}

}

void MasterAgent::persist_failed_task(const FailedTaskRecord& record) {
    // failed_tasks.bin 读改写互斥：调用方横跨 lane handler（on_task_complete/
    // on_task_failed）与后台线程（attr-tick/watchdog 经 schedule_tasks）。
    // 落点按 record 归属 db 解析（Task db 归属规则）。
    std::lock_guard<std::mutex> lk(failed_tasks_file_mutex_);
    CMString file_path = get_failed_tasks_file_path(record.submission_.owner_db_path_);
    append_failed_record(file_path, record);

    ERR("Task {} (owner_db={}) failed and persisted. To restart after fixing, "
        "call restart_failed_tasks([\"{}\"])",
        record.task_id_, record.submission_.owner_db_path_,
        record.submission_.owner_db_path_);
    if (metrics_db_) {
        metrics_db_->record_task_event(record.task_id_, 0, "PERSIST_FAILED");
    }
}

void MasterAgent::remove_persisted_task(uint64_t task_id) {
    // 成功摘除按归属文件定位：owner 从 metadata 的 submission_ 读（记录与
    // 摘除同源，不依赖文件扫描）。internal task 无 metadata（也未 persist 过）。
    auto md = metadata_->get_task(task_id);
    if (!md) return;

    std::lock_guard<std::mutex> lk(failed_tasks_file_mutex_);
    CMString file_path = get_failed_tasks_file_path(md->submission_.owner_db_path_);
    if (!std::filesystem::exists(file_path)) return;

    auto records = read_failed_records(file_path);
    if (records.empty()) return;

    size_t before = records.size();
    records.erase(
        std::remove_if(records.begin(), records.end(),
            [task_id](const FailedTaskRecord& r) { return r.task_id_ == task_id; }),
        records.end());

    if (records.size() == before) return;

    if (records.empty()) {
        std::filesystem::remove(file_path);
        INFO("Removed persisted task {}, file empty, deleted", task_id);
    } else {
        rewrite_failed_records(file_path, records);
        INFO("Removed persisted task {} from {}", task_id, file_path);
    }
}

void MasterAgent::register_db_uid(const CMString& uid, const CMString& db_path) {
    // uid 迁移/merge 不变（跨路径稳定键）；索引值覆盖式更新（load_db 复 load
    // 幂等）。merge 改路径后由新路径上报覆盖，旧条目残留无害（uid 仍指向
    // 最新一次注册）。
    db_uid_index_.update(uid, [&](CMString& v) { v = db_path; });
}

size_t MasterAgent::restart_failed_tasks(const CMString& file_path) {
    // 文件互斥只覆盖 读取+校验+删除：下面的 submit_task → schedule_tasks →
    // persist_failed_task 会再取同一把锁，持锁重提交 = 自死锁（graceful_shutdown
    // QA 实测：主线程同时持有 schedule_mutex_ + failed_tasks_file_mutex_ 后挂死）。
    CMVector<FailedTaskRecord> records;
    {
        std::lock_guard<std::mutex> lk(failed_tasks_file_mutex_);
        if (!std::filesystem::exists(file_path)) {
            WARN("No failed tasks file found at {}", file_path);
            return 0;
        }

        records = read_failed_records(file_path);
        if (records.empty()) {
            WARN("No failed tasks to restart");
            return 0;
        }

        // ── uid 解析（文件级原子）：bin 内任一 db 引用无法解析 → 整 bin 拒绝。
        // 记录内的路径是提交时快照，db 目录迁移后失真；uid 是跨路径稳定键，
        // 命中运行时索引即得当前路径（args/inputs/vars/owner 一并自愈）。
        // 逐记录先收集全局 old→new 映射（跨 db task 的 inputs 可能引用其他
        // 记录 args 携带的 db），再统一替换。
        CMUnorderedMap<CMString, CMString> old_to_new;   // 旧 db_path → 当前
        for (const auto& record : records) {
            for (const auto& arg : record.submission_.args_) {
                auto [uid, db_path] = parse_db_arg_uid(arg);
                if (db_path.empty()) continue;   // 非 db 参数
                if (uid.empty()) {
                    ERR("Cannot restart failed tasks from '{}': task {} arg has no "
                        "db uid (legacy format, unresolvable after migration): {}",
                        file_path, record.task_id_, arg);
                    return 0;
                }
                auto current = db_uid_index_.find(uid);
                if (!current.has_value() || current->empty()) {
                    ERR("Cannot restart failed tasks from '{}': db uid={} (expected "
                        "at {}) is not loaded in this run — load it (load_db/"
                        "load_project) then restart again",
                        file_path, uid, db_path);
                    return 0;
                }
                if (*current != db_path) {
                    old_to_new[db_path] = *current;
                }
            }
        }

        // 统一替换：args 重编码为 v2（当前路径）；inputs/vars/outputs 按
        // old→new 做 "{old}:" 前缀精确替换。
        auto replace_prefix = [&old_to_new](CMString& full) {
            if (full.empty()) return;
            auto pos = full.rfind(':');
            if (pos == CMString::npos || pos == 0) return;
            auto it = old_to_new.find(full.substr(0, pos));
            if (it != old_to_new.end()) {
                full = it->second + full.substr(pos);
            }
        };
        for (auto& record : records) {
            for (auto& arg : record.submission_.args_) {
                auto [uid, db_path] = parse_db_arg_uid(arg);
                if (db_path.empty()) continue;
                auto current = db_uid_index_.find(uid);
                // v2 统一格式（旧 4 段升级；data_path 权威在 _DB_META）。
                arg = "__fly_db2__:" + uid + ":" + (*current);
            }
            for (auto& in : record.submission_.inputs_) replace_prefix(in);
            for (auto& v : record.submission_.vars_) replace_prefix(v);
            for (auto& o : record.submission_.outputs_) replace_prefix(o);

            // 位置即归属：bin 所在目录就是归属 db 的当前路径，记录内 owner
            // 是提交时快照（迁移后失真）；空 owner（fallback 记录）保持空。
            if (!record.submission_.owner_db_path_.empty()) {
                record.submission_.owner_db_path_ =
                    std::filesystem::path(file_path).parent_path().string();
            }
        }
        // write_context_hash 保持记录原值：provenance 仅做相等比较（同 hash
        // 幂等 / 新对象首写放行），重算反而触发 WRITE_PROVENANCE_MISMATCH。

        std::filesystem::remove(file_path);
    }

    size_t record_count = records.size();
    INFO("Restarting {} failed tasks", record_count);
    INFO("Cleared failed tasks file {}", file_path);

    for (auto& record : records) {
        metadata_->remove_task(record.task_id_);
        // record.submission_ 携带完整的提交字段（含 priority/attribute_timeout/vars/
        // owner_db_path），整体传入 submit_task，消除逐字段复制的错位/漏传风险。
        submit_task(record.task_id_, record.submission_);
        if (metrics_db_) {
            metrics_db_->record_task_event(record.task_id_, 0, "RESTART");
        }
    }

    INFO("Restarted {} failed tasks", record_count);
    return record_count;
}

void MasterAgent::setup_write_context() {
    // master 自写对象的 record 阶段无需处理（register 已含全部 placement/schedule 逻辑）。
    // 留一个空 record_write_func 仅满足 is_active() 探测，不触发任何动作。
    WorkerAgentContext::set_record_write_func([](const CMString&, const CMString&, int64_t) {});
    WorkerAgentContext::set_register_func([this](const CMString& db_path, const CMString& name, int64_t compressed_size, bool preliminary) -> std::pair<CMString, TaskErrorType> {
        return on_master_register_write(db_path, name, compressed_size, preliminary);
    });
    WorkerAgentContext::set_freeze_func([this](const CMString& db_path) {
        on_master_freeze(db_path);
    });
    // master 进程内 remove_object：同步清 provenance + 通知 worker（原 request_remove no-op，
    // 导致 master remove 不清 provenance，阻塞合法的 remove+rewrite 流程）。
    WorkerAgentContext::set_remove_request_func([this](const CMString& db_path, const CMString& object_name) {
        on_master_remove(db_path, object_name);
    });
    // Var funcs: master process operates directly on the authoritative Database
    // store (no network). The context passes FULL var names (db_path:short_name);
    // split off db_path to locate the Database, then query with the short name.
    // 锁内只 find + 拷 shared_ptr（var 族 var_mutex_ 自保护，D2 拆除）。
    auto find_db = [this](const CMString& db_path) -> CMSharedPtr<Database> {
        std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto it = db_instances_.find(db_path);
        return it != db_instances_.end() ? it->second : nullptr;
    };
    WorkerAgentContext::set_set_var_func([this, find_db](const CMString& full_var_name,
                                                FlyBufferPtr value, const CMString& type_name) -> bool {
        auto [db_path, short_name] = split_full_name(full_var_name);
        if (db_path.empty()) return false;
        auto db = find_db(db_path);
        if (!db) return false;
        return db->master_set_var(short_name, value, type_name);
    });
    WorkerAgentContext::set_get_var_func([this, find_db](const CMString& full_var_name)
        -> std::tuple<bool, FlyBufferPtr, CMString> {
        auto [db_path, short_name] = split_full_name(full_var_name);
        if (db_path.empty()) return {false, nullptr, ""};
        auto db = find_db(db_path);
        if (!db) return {false, nullptr, ""};
        return db->master_get_var(short_name);
    });
    WorkerAgentContext::set_remove_var_func([this, find_db](const CMString& full_var_name) {
        auto [db_path, short_name] = split_full_name(full_var_name);
        if (!db_path.empty()) {
            auto db = find_db(db_path);
            if (db) {
                db->master_remove_var(short_name);
                broadcast_var(full_var_name, false);
            }
        }
    });
}

std::pair<CMString, TaskErrorType> MasterAgent::on_master_register_write(const CMString& db_path, const CMString& name, int64_t compressed_size, bool preliminary) {
    if (!running_.load()) {
        // master 未运行（停止窗口）无法裁决注册——按注册被拒绝归类（原 {"",UNKNOWN}
        // 会被调用方当成功放行，写未经 provenance 裁决）。
        return {"master not running", TaskErrorType::WRITE_REGISTRATION_FAILED};
    }
    // master 自写走统一的 WriteRegisterMessage 路径（worker_id=0），与 worker 行为对称。
    // 同步调用 do_write_register，丢弃 ack（master 自写无需网络 ACK）。
    WriteRegisterMessage msg;
    msg.worker_id_ = 0;
    msg.object_name_ = db_path + ":" + name;
    msg.db_path_ = db_path;
    msg.size_bytes_ = compressed_size;
    msg.preliminary_ = preliminary;
    // master 自写经 commit_write 已填时间戳（若 current_write_hash 空），此处取到非空，
    // 使 do_write_register 的 provenance 校验对 master 自写也生效（原漏设导致无保护）。
    msg.write_context_hash_ = WorkerAgentContext::get_current_write_hash();
    if (msg.write_context_hash_.empty()) {
        // 经 commit_write 时 guard 已填时间戳（此处取到非空）；未经 commit_write 的纯登记
        // 路径（current_write_hash 空），用时间戳 fallback 保证非空，使 provenance 校验生效。
        msg.write_context_hash_ = make_timestamp_hash();
    }
    {
        // 锁内只 find + 拷 writer_id（值拷贝）——do_write_register 的完整登记链
        //（provenance、frozen、stream 模式 mark_data_ready、record_worker_info、
        // schedule_tasks）移出容器锁：原实现持 db 读锁跑全链，且 record_worker_info
        // 会递归获取同一 shared_mutex（写者排队时潜在死锁）。与 worker 路径
        // on_write_register（锁外裸调）对称。
        std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto db_it = db_instances_.find(db_path);
        if (db_it != db_instances_.end()) {
            msg.writer_id_ = db_it->second->get_writer_id();
        }
    }
    auto ack = do_write_register(msg);
    if (!ack.success_) {
        return {ack.error_message_, ack.error_type_};
    }
    // 成功：返回空 error_message + UNKNOWN（TaskErrorType::UNKNOWN 在本 codebase 语义为"无错误"哨兵，
    // 多处 `if (err_type != UNKNOWN)` 据此判断失败，与字面"未知错误"无关，沿用既有约定）。
    return {"", TaskErrorType::UNKNOWN};
}

CMVector<IndexEntry> MasterAgent::restore_master_idx(const CMString& db_path,
                                                                                                              const CMString& writer_id) {
    CMString idx_path = db_path + "/" + writer_id + ".idx";
    if (!std::filesystem::exists(idx_path)) {
        WARN("restore_master_idx: idx file not found: {}", idx_path);
        return {};
    }

    LocalIndex idx(idx_path);
    idx.load();
    auto entries = idx.get_all_entries();

    if (!entries.empty()) {
        DataService::instance()->restore_entries(db_path, entries);
        // entry.object_name_ 是 short_name（LocalIndex 不再存 db_path 前缀），
        // DependencyGraph 用 full_name 作 key，这里拼接。
        bool db_frozen = std::filesystem::exists(db_path + "/_FROZEN");
        for (const auto& entry : entries) {
            graph_->mark_data_ready(db_path + ":" + entry.object_name_);
            // Part B: 从 idx entry 重建 write_provenance_（load 未 freeze db 时）。
            // frozen db 不重建——freeze 后 do_write_register 第一道 is_db_frozen 拦下所有写入，
            // provenance 已无写入可拦截，重建无意义（Part C 会在 freeze 时清理）。
            if (!db_frozen && !entry.write_context_hash_.empty()) {
                std::lock_guard<std::mutex> lk(provenance_mutex_);
                write_provenance_[db_path][entry.object_name_] = entry.write_context_hash_;
            }
        }
        INFO("restore_master_idx: restored {} entries for db_path={}", entries.size(), db_path);
    }

    return entries;
}

CMVector<IndexEntry> MasterAgent::read_idx_entries(const CMString& db_path,
                                                    const CMString& writer_id) {
    CMString idx_path = db_path + "/" + writer_id + ".idx";
    if (!std::filesystem::exists(idx_path)) {
        WARN("read_idx_entries: idx file not found: {}", idx_path);
        return {};
    }
    LocalIndex idx(idx_path);
    idx.load();
    return idx.get_all_entries();
}

void MasterAgent::send_idx_load_commands(const CMString& db_path,
                                                                                      const CMVector<CMString>& writer_ids) {
    IdxLoadCommandMessage msg;
    msg.db_path_ = db_path;
    msg.writer_ids_ = writer_ids;

    for (const auto& [worker_id, conn_id] : snapshot_worker_conns()) {
        reactor_->send(conn_id, msg);
        INFO("Sent IdxLoadCommand to worker_id={}: db_path={}, writer_ids_count={}",
             worker_id, db_path, writer_ids.size());
    }
}

void MasterAgent::rebuild_remote_idx(const CMString& db_path,
                                                                              const CMVector<::WorkerInfo>& workers) {
    CMUnorderedMap<CMString, CMString> old_id_to_hostname;
    for (const auto& w : workers) {
        old_id_to_hostname[std::to_string(w.worker_id_)] = w.hostname_;
    }

    CMUnorderedMap<CMString, CMVector<uint64_t>> hostname_to_new_workers;
    for (const auto& info : worker_manager_->get_all_workers()) {
        hostname_to_new_workers[info.hostname_].push_back(info.worker_id_);
    }

    for (const auto& w : workers) {
        if (w.writer_id_.empty()) {
            WARN("rebuild_remote_idx: empty writer_id for worker_id={}", w.worker_id_);
            continue;
        }

        CMString idx_path = db_path + "/" + w.writer_id_ + ".idx";
        if (!std::filesystem::exists(idx_path)) {
            WARN("rebuild_remote_idx: idx file not found: {}", idx_path);
            continue;
        }

        LocalIndex idx(idx_path);
        idx.load();
        auto entries = idx.get_all_entries();

        auto host_it = old_id_to_hostname.find(std::to_string(w.worker_id_));
        if (host_it == old_id_to_hostname.end()) {
            WARN("rebuild_remote_idx: no hostname for worker_id={}", w.worker_id_);
            continue;
        }

        const CMString& hostname = host_it->second;
        auto new_it = hostname_to_new_workers.find(hostname);
        if (new_it == hostname_to_new_workers.end() || new_it->second.empty()) {
            WARN("rebuild_remote_idx: no new worker for hostname={}", hostname);
            continue;
        }

        uint64_t new_worker_id = new_it->second[0];
        auto addr = DataService::instance()->get_worker_address(new_worker_id);

        for (const auto& entry : entries) {
            // entry.object_name_ 是 short_name（LocalIndex 不再存 db_path 前缀），拼接 full。
            CMString full = db_path + ":" + entry.object_name_;
            DataService::instance()->update_remote_idx(full, new_worker_id, addr.host_, addr.port_);
            graph_->mark_data_ready(full);
        }
        INFO("rebuild_remote_idx: mapped {} entries from writer_id={} to new worker_id={}",
             entries.size(), w.writer_id_, new_worker_id);
    }
}

void MasterAgent::set_master_hostname(const CMString& hostname) {
    ProcessInfo::instance()->set_hostname(hostname);
}

void MasterAgent::send_idx_load_to_worker(const CMString& db_path,
                                                                                        const CMVector<CMString>& writer_ids,
                                            uint64_t worker_id) {
    IdxLoadCommandMessage msg;
    msg.db_path_ = db_path;
    msg.writer_ids_ = writer_ids;

    uint64_t conn = lookup_worker_conn(worker_id);
    if (conn == 0) {
        ERR("send_idx_load_to_worker: worker_id={} not found", worker_id);
        return;
    }
    // 可见性屏障登记先于 send：Ack 可能极快返回（本地网络），登记晚了会漏减
    // 计数导致 load_db 等待侧死等到超时。per-worker 集合（重复 send 同 worker
    // 幂等，remaining 与集合同步）。
    pending_idx_loads_.with_lock([&](auto& m) {
        auto it = m.find(db_path);
        if (it == m.end()) {
            auto p = std::make_shared<PendingIdxLoad>();
            p->pending_workers_.insert(worker_id);
            p->remaining_ = 1;
            m[db_path] = p;
        } else {
            it->second->pending_workers_.insert(worker_id);
            it->second->remaining_ =
                static_cast<int32_t>(it->second->pending_workers_.size());
        }
    });
    reactor_->send(conn, msg);
    INFO("Sent IdxLoadCommand to worker_id={}: db_path={}, writer_ids_count={}",
         worker_id, db_path, writer_ids.size());
}

int32_t MasterAgent::idx_load_pending(const CMString& db_path) {
    auto p = pending_idx_loads_.find(db_path);
    return p ? p->remaining_ : 0;
}

void MasterAgent::rebuild_remote_idx_for_worker(const CMString& db_path,
                                                                                                      const CMVector<CMString>& writer_ids,
                                                   uint64_t worker_id) {
    auto addr = DataService::instance()->get_worker_address(worker_id);

    for (const auto& writer_id : writer_ids) {
        CMString idx_path = db_path + "/" + writer_id + ".idx";
        if (!std::filesystem::exists(idx_path)) {
            WARN("rebuild_remote_idx_for_worker: idx file not found: {}", idx_path);
            continue;
        }

        LocalIndex idx(idx_path);
        idx.load();
        if (idx.had_unclosed_segment()) {
            WARN("rebuild_remote_idx: detected unclosed write segment in {} "
                 "(crashed task), its entries were discarded", idx_path);
        }
        auto entries = idx.get_all_entries();

        for (const auto& entry : entries) {
            // entry.object_name_ 是 short_name（LocalIndex 不再存 db_path 前缀），拼接 full。
            CMString full = db_path + ":" + entry.object_name_;
            DataService::instance()->update_remote_idx(full, worker_id, addr.host_, addr.port_);
            graph_->mark_data_ready(full);
        }
        INFO("rebuild_remote_idx_for_worker: mapped {} entries from writer_id={} to worker_id={}",
             entries.size(), writer_id, worker_id);

        // temp 落盘同批恢复（task 级断点）：temp 对象与正式对象同 worker 持有，
        // 一并 update_remote_idx + mark_data_ready——重投的下游 task 输入就绪。
        // frozen db 的 temp 已随 freeze 清理，无 temp idx 属正常。
        CMString temp_idx_path = db_path + "/.temp." + writer_id + ".idx";
        if (std::filesystem::exists(temp_idx_path)) {
            LocalIndex temp_idx(temp_idx_path);
            temp_idx.load();
            auto temp_entries = temp_idx.get_all_entries();
            for (const auto& entry : temp_entries) {
                CMString full = db_path + ":" + entry.object_name_;
                DataService::instance()->update_remote_idx(full, worker_id, addr.host_, addr.port_);
                graph_->mark_data_ready(full);
            }
            if (!temp_entries.empty()) {
                INFO("rebuild_remote_idx_for_worker: mapped {} temp entries from "
                     "writer_id={} to worker_id={}", temp_entries.size(), writer_id, worker_id);
            }
        }
    }
}

void MasterAgent::on_idx_load_ack(uint64_t conn_id, const IdxLoadAckMessage& msg) {
    INFO("IdxLoadAck: worker_id={}, db_path={}, success={}, loaded_count={}, writer_ids={}",
         msg.worker_id_, msg.db_path_, msg.success_, msg.loaded_count_, msg.loaded_writer_ids_.size());

    if (!msg.success_) {
        ERR("IdxLoadAck failed from worker_id={}: {}", msg.worker_id_, msg.error_message_);
        // 失败也必须消化屏障计数（置 -1 哨兵），否则 load_db 等待侧死等超时。
        pending_idx_loads_.complete(msg.db_path_, [](PendingIdxLoad& p) {
            p.remaining_ = -1;
        });
        return;
    }

    // Master reads the same idx files from shared filesystem and updates remote_idx_
    // 锁内只做存在性判断：rebuild 读磁盘 idx 文件的重活移出容器锁（D2 拆除——
    // 它根本不使用 Database 对象本身）。
    {
        std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        if (db_instances_.find(msg.db_path_) == db_instances_.end()) {
            ERR("IdxLoadAck: unknown db_path={}", msg.db_path_);
            pending_idx_loads_.complete(msg.db_path_, [](PendingIdxLoad& p) {
                p.remaining_ = -1;
            });
            return;
        }
    }

    rebuild_remote_idx_for_worker(msg.db_path_, msg.loaded_writer_ids_, msg.worker_id_);

    // rebuild 落地（remote_idx 更新 + mark_data_ready）之后才递减——等待侧看到
    // 0 时对象位置必须已可见。per-worker erase 幂等：重复/重放 Ack 不会吃掉
    // 其他 worker 的份额（总数计数会提前归 0 击穿屏障）。
    pending_idx_loads_.complete(msg.db_path_, [wid = msg.worker_id_](PendingIdxLoad& p) {
        p.pending_workers_.erase(wid);
        p.remaining_ = static_cast<int32_t>(p.pending_workers_.size());
    });
}

void MasterAgent::on_database_freeze_request(uint64_t conn_id, const DatabaseFreezeNotification& msg) {
    bool streaming_mode = (Config::instance()->get_int("dependency_update_mode") == 0);

    DatabaseFreezeAckMessage ack;
    ack.db_path_ = msg.db_path_;
    bool accepted = false;

    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        bool already = frozen_dbs_.count(msg.db_path_) > 0 || pending_frozen_dbs_.count(msg.db_path_) > 0;
        if (already) {
            // 冲突：db 已被本 task 或其他 task freeze（业务流程错误）→ fail-fast
            ack.success_ = false;
            ack.error_type_ = TaskErrorType::DB_ALREADY_FROZEN;
            WARN("Freeze rejected (already frozen/pending): db_path={}, task_id={}",
                 msg.db_path_, msg.task_id_);
        } else if (streaming_mode) {
            // stream 模式：即时确认（保持原语义）
            frozen_dbs_.insert(msg.db_path_);
            ack.success_ = true;
            accepted = true;
            INFO("DatabaseFreezeRequest (stream): db_path={}", msg.db_path_);
        } else {
            // 非 stream 模式：登记 pending（记 task_id），不广播、不本地 freeze
            pending_frozen_dbs_[msg.db_path_] = msg.task_id_;
            ack.success_ = true;
            accepted = true;
            INFO("DatabaseFreezeRequest (non-stream pending): db_path={}, task_id={}",
                 msg.db_path_, msg.task_id_);
        }
    }

    // 回 ack（两种模式都回，让 worker 同步确认 freeze 是否被接受）
    reactor_->send(conn_id, ack);

    if (accepted && streaming_mode) {
        // Part C: freeze 确认后 provenance 已无写入可拦截（is_db_frozen 拦下所有后续写入），
        // 立即清理释放内存。
        cleanup_provenance_for_db(msg.db_path_);
        // RunSummary：封闭 db 窗口 + du 统计磁盘终值（锁外，ms 级）。
        if (run_metrics_) run_metrics_->record_db_frozen(msg.db_path_);
        if (metrics_db_) {
            metrics_db_->record_event("db", "DB_FROZEN", 0, 0, msg.db_path_);
            record_db_du(msg.db_path_);
        }
        // stream 模式的本地 freeze + 广播（D2 拆除：freeze 重 IO 与广播移出容器锁）
        CMSharedPtr<Database> db;
        {
            std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
            auto it = db_instances_.find(msg.db_path_);
            if (it != db_instances_.end()) db = it->second;
        }
        if (db) db->freeze();
        DatabaseFreezeNotification broadcast_msg = msg;
        for (const auto& [wid, cid] : snapshot_worker_conns()) {
            (void)wid;
            reactor_->send(cid, broadcast_msg);
        }
        INFO("DB frozen and broadcasted (stream): db_path={}", msg.db_path_);
    }
}

void MasterAgent::on_master_freeze(const CMString& db_path) {
    if (!running_.load()) return;

    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        if (frozen_dbs_.count(db_path)) {
            WARN("DB already frozen, ignoring duplicate freeze: db_path={}", db_path);
            return;
        }

        frozen_dbs_.insert(db_path);
    }

    // Part C: freeze 确认后 provenance 已失效，立即清理释放内存。
    cleanup_provenance_for_db(db_path);
    // RunSummary：封闭 db 窗口 + du 统计磁盘终值（锁外，ms 级）。
    if (run_metrics_) run_metrics_->record_db_frozen(db_path);
    if (metrics_db_) {
        metrics_db_->record_event("db", "DB_FROZEN", 0, 0, db_path);
        record_db_du(db_path);
    }

    DatabaseFreezeNotification msg;
    msg.db_path_ = db_path;
    for (const auto& [wid, cid] : snapshot_worker_conns()) {
        (void)wid;
        reactor_->send(cid, msg);
    }

    // 流程 message：master 直接 freeze 完成（不可逆里程碑）。
    MSG("STOR::0001", 2, "db {} frozen (master direct)", db_path);
}

void MasterAgent::trigger_graceful_shutdown() {
    // 幂等：只拉起一次退出线程。stop_impl 内部 draining_.exchange(true) 亦防重入。
    if (graceful_stop_started_.exchange(true)) return;
    INFO("Graceful shutdown requested (SIGTERM), starting fast_exit (skip drain)");
    // 独立线程执行：fast_exit 会 join 本（heartbeat）线程，不能在自身上调用。
    std::thread([this]() {
        fast_exit("SIGTERM received");
    }).detach();
}

void MasterAgent::notify_drain_if_active() {
    if (draining_.load()) {
        // 持锁 notify（规范）：无锁 notify 存在 lost wakeup 窗口——drain 主线程
        // 「查完条件 RUNNING>0」与「进入 wait」之间收到无锁 notify 会落空，
        // complete 已把 RUNNING 归零但 drain 睡满整个超时（EndToEnd 4 实例
        // 压测实测 300s 卡死，[SD] 链铁证：drain waiting 后 on_complete OK
        // now_running=0 但 drain 未醒）。旧 30s 超时一直掩盖此违规。
        std::lock_guard<std::mutex> lk(drain_mutex_);
        drain_cv_.notify_all();
    }
}

void MasterAgent::persist_pending_tasks() {
    auto pending = metadata_->get_tasks_by_status(TaskStatus::PENDING);
    if (pending.empty()) return;

    INFO("Persisting {} pending tasks on shutdown", pending.size());
    for (const auto& task : pending) {
        auto record = make_failed_record(task->task_id_,
                                         "Master shutdown: task still pending");
        persist_failed_task(record);
    }
}

FailedTaskRecord MasterAgent::make_failed_record(uint64_t task_id, const CMString& error_msg) {
    FailedTaskRecord record;
    record.task_id_ = task_id;
    record.error_message_ = error_msg;
    // 整体拷贝 submission_——新增字段自动包含，无需手动逐字段同步。
    if (auto t = metadata_->get_task(task_id)) {
        record.submission_ = t->submission_;
    }
    return record;
}

void MasterAgent::on_backup_request(uint64_t conn_id, const BackupRequestMessage& msg) {
    INFO("BackupRequest: object={}, source_worker={}", msg.object_name_, msg.worker_id_);

    uint64_t backup_worker_id = select_backup_worker(msg.object_name_);
    if (backup_worker_id == 0) {
        ERR("No suitable backup worker found for object={}", msg.object_name_);
        return;
    }

    uint64_t backup_conn = 0;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        auto backup_conn_it = worker_to_conn_.find(backup_worker_id);
        if (backup_conn_it == worker_to_conn_.end()) {
            ERR("Backup worker {} not connected", backup_worker_id);
            return;
        }
        backup_conn = backup_conn_it->second;
    }

    uint64_t backup_task_id = remote_task_counter_.fetch_add(1);

    // Problem 4：backup task 的 assign_task 纳入 schedule_mutex_，与 scheduler 的
    // get_idle_workers→assign 序列互斥，避免 backup assign 与 scheduler assign 交错产生
    // worker_manager 撕裂状态（同一 worker 被两边几乎同时赋不同 task）。
    // on_backup_request 的调用方（reactor dispatch / trigger_auto_backup /
    // evaluate_and_maybe_backup←on_worker_backup_suggest）均不持有 schedule_mutex_，无递归死锁。
    {
        std::lock_guard<std::mutex> lk(schedule_mutex_);
        worker_manager_->assign_task(backup_worker_id, backup_task_id);
    }

    CMString short_name = msg.object_name_;
    CMString prefix = msg.db_path_ + ":";
    if (short_name.substr(0, prefix.size()) == prefix) {
        short_name = short_name.substr(prefix.size());
    }

    TaskAssignMessage assign;
    assign.task_id_ = backup_task_id;
    assign.task_name_ = "__backup_object";
    assign.task_module_ = "__fly_internal";
    assign.args_ = {short_name, msg.db_path_};

    {
        auto [b_db, b_short] = fly::split_full_name(msg.object_name_);
        assign.write_context_hash_ = provenance_lookup(b_db, b_short);
    }

    reactor_->send(backup_conn, assign);
    INFO("Backup task assigned to worker_id={} for object={}", backup_worker_id, msg.object_name_);
}

uint64_t MasterAgent::select_backup_worker(const CMString& object_name) {
    // 现有副本的 worker_id（既是数据源，也是 host 去重的排除集）。
    auto holders = DataService::instance()->get_remote_workers(object_name);
    CMUnorderedSet<uint64_t> holder_set(holders.begin(), holders.end());

    CMVector<WorkerInfo> all;
    CMUnorderedSet<CMString> holder_hosts;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        all = worker_manager_->get_all_workers();  // 一次快照（含 hostname_/role_）
        for (const auto& info : all) {
            if (holder_set.count(info.worker_id_)) holder_hosts.insert(info.hostname_);
        }
    }

    // 候选评估三级 key：
    //   1. host-disjoint——host 故障域隔离，最高优先，不因 role 让步；
    //   2. storage_only 优先——不跑用户 task，进程可靠且数据面资源稳定；
    //   3. 名下副本字节最轻——磁盘水位，防副本向少数存储节点倾斜。
    // host 全冲突时 best-effort 回退到「无副本」候选（保证副本数增长），
    // 回退层内同样按 2/3 排序。
    CMUnorderedSet<uint64_t> candidate_ids;
    for (const auto& info : all) {
        if (holder_set.count(info.worker_id_)) continue;  // 已有副本，跳过
        if (info.worker_id_ == 0) continue;                // master 不做 backup 目标
        if (!worker_status_alive(info.status_)) continue;
        candidate_ids.insert(info.worker_id_);
    }
    auto worker_bytes = DataService::instance()->get_worker_bytes_batch(candidate_ids);

    uint64_t best = 0, fallback_best = 0;
    bool best_storage = false, fallback_best_storage = false;
    int64_t best_bytes = INT64_MAX, fallback_best_bytes = INT64_MAX;
    for (const auto& info : all) {
        if (!candidate_ids.count(info.worker_id_)) continue;
        bool storage = (info.role_ == WorkerRole::STORAGE_ONLY);
        int64_t bytes = worker_bytes[info.worker_id_];  // 批量结果已含全部候选，缺省 0

        if (!holder_hosts.count(info.hostname_)) {
            // host 全新层。
            if (best == 0 || (storage && !best_storage) ||
                (storage == best_storage && bytes < best_bytes)) {
                best = info.worker_id_;
                best_storage = storage;
                best_bytes = bytes;
            }
        } else if (fallback_best == 0 || (storage && !fallback_best_storage) ||
                   (storage == fallback_best_storage && bytes < fallback_best_bytes)) {
            fallback_best = info.worker_id_;
            fallback_best_storage = storage;
            fallback_best_bytes = bytes;
        }
    }

    if (best != 0) {
        if (best_storage) {
            INFO("select_backup_worker: obj={} → worker {} (host-disjoint + storage-only, "
                 "load={}B)", object_name, best, best_bytes);
        }
        return best;
    }
    if (fallback_best != 0) {
        INFO("select_backup_worker: no host-disjoint worker for obj={}, fallback to {}",
             object_name, fallback_best);
    }
    return fallback_best;
}

void MasterAgent::trigger_auto_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_path) {
    INFO("Auto-backup triggered: object={}, source_worker={}", object_name, source_worker_id);
#ifdef FLY_ENABLE_TEST_HOOKS
    ++auto_backup_trigger_count_for_testing_;
#endif

    BackupRequestMessage backup_msg;
    backup_msg.worker_id_ = source_worker_id;
    backup_msg.object_name_ = object_name;
    backup_msg.db_path_ = db_path;

    on_backup_request(0, backup_msg);
    // 新设计：master 不再 decay_after_backup。backup → replicas++ → score = cumulative/replicas
    // 自然下降，降到 < threshold 即停。cumulative 不 reset（reset 会导致持续热对象反复 backup）。
}

void MasterAgent::on_worker_backup_suggest(uint64_t conn_id, const WorkerBackupSuggestMessage& msg) {
    DBG("WorkerBackupSuggest: worker={}, obj={}, delta_count={}, delta_bytes={}, size={}",
        msg.worker_id_, msg.object_name_, msg.delta_count_, msg.delta_bytes_, msg.size_bytes_);
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // EWMA 衰减：基于 suggest 到达频率（不受单次传输时间影响 → 不惩罚大对象）。
    double dps = Config::instance()->get_int("master_ewma_decay_per_sec") / 100.0;
    backup_scores_.update(msg.object_name_, [&](ObjectBackupScore& s) {
        int64_t elapsed = now - s.last_suggest_time_;
        if (elapsed > 0 && s.last_suggest_time_ > 0) {
            double factor = std::pow(1.0 - dps, static_cast<double>(elapsed));
            s.cumulative_bytes_ *= factor;
            s.cumulative_count_ *= factor;
        }
        s.cumulative_bytes_ += static_cast<double>(msg.delta_bytes_);
        s.cumulative_count_ += static_cast<double>(msg.delta_count_);
        if (msg.size_bytes_ > 0) s.size_bytes_ = msg.size_bytes_;
        s.last_suggest_time_ = now;
    });
    evaluate_and_maybe_backup(msg.object_name_);
}

void MasterAgent::evaluate_and_maybe_backup(const CMString& object_name) {
    auto holders = DataService::instance()->get_remote_workers(object_name);
    uint32_t replicas = static_cast<uint32_t>(holders.size());
    double divisor = std::max(static_cast<double>(replicas), 1.0);

    // find + 默认值（不再像 operator[] 那样在 map 残留空条目；数值等价）。
    ObjectBackupScore s = backup_scores_.find(object_name).value_or(ObjectBackupScore{});

    double score_bytes = s.cumulative_bytes_ / divisor;
    double score_count = s.cumulative_count_ / divisor;
    double bytes_thr = static_cast<double>(Config::instance()->get_int("backup_bytes_threshold"));
    double count_thr = static_cast<double>(Config::instance()->get_int("backup_count_threshold"));

    // 双分数 OR：字节或次数任一超阈值即视为热点。
    bool hot_bytes = bytes_thr > 0 && score_bytes >= bytes_thr;
    bool hot_count = count_thr > 0 && score_count >= count_thr;
    if (!hot_bytes && !hot_count) return;
    if (holders.empty()) return;

    // 副本上限：正常 max_backup_replicas；大文件 + 异常高分可突破上限。
    uint32_t max_r = static_cast<uint32_t>(Config::instance()->get_int("max_backup_replicas"));
    bool large_exception =
        (s.size_bytes_ >= Config::instance()->get_int("backup_large_object_threshold")
         && score_bytes >= static_cast<double>(Config::instance()->get_int("backup_high_score_threshold")));
    uint32_t cap = large_exception
        ? max_r + static_cast<uint32_t>(Config::instance()->get_int("backup_extra_slots"))
        : max_r;
    if (replicas >= cap) {
        DBG("[AUTO-BACKUP] obj={}, replicas={}, cap={}, score_bytes={}, score_count={} → at cap, skip",
            object_name, replicas, cap, score_bytes, score_count);
        return;
    }

    auto [db_path, short_name] = fly::split_full_name(object_name);
    INFO("[AUTO-BACKUP] obj={}, replicas={}, score_bytes={}, score_count={} → triggering backup",
         object_name, replicas, score_bytes, score_count);
    if (metrics_db_) {
        metrics_db_->record_event("storage", "AUTO_BACKUP_TRIGGER", holders.front(), 0,
                                  object_name);
    }
    // 每次 suggest 触发一份 backup（async：BackupComplete 后 replicas 才增长）。
    // score/replicas 反馈平衡：backup → replicas++ → score 降 → 自然收敛，避免一次循环多 backup
    // 落到同一 worker（select_backup_worker 对同一 source 会重复选同一目标）。
    trigger_auto_backup(object_name, holders.front(), db_path);
}

// =============================================================================
// DB Merge support — fly.merge_db 主动 API。详见 docs/db-merge-design.md §3.4。
// =============================================================================

uint64_t MasterAgent::send_merge_task(uint64_t target_worker_id,
                                       const CMString& short_name,
                                       const CMString& source_db_path,
                                       const CMString& target_db_path,
                                       const CMString& target_data_path,
                                       const CMString& source_host) {
    uint64_t merge_task_id = remote_task_counter_.fetch_add(1);

    // 登记初始 pending 状态（wait_merge_tasks_complete 等待此表）。
    // db_path_：失败清理按 db 精确匹配（跨 db 并发 merge 不互扰）。
    // worker_id_ 必须在发起时记录：settle_pending_for_dead_worker 按
    // 「!completed_ && worker_id_==死亡worker」联动判死——原实现仅在成功
    // 完成路径写入（pending 期恒 0），merge worker 判死后 wait 无限挂起。
    auto state = CMMakeShared<MergeTaskState>();
    state->db_path_ = source_db_path;
    state->worker_id_ = target_worker_id;
    merge_task_states_.emplace(merge_task_id, state);

    CMString full_name = source_db_path + ":" + short_name;
    // 把源对象位置注入 task dependency_locations_，让 target worker 的 read_raw_compressed
    // 直接 TIER2 命中（无需 TIER3 回查 master）。源位置从 remote_idx 取排序后
    // 首选（storage 优先/死 holder 排尾，与 assign 预取同一语义）。
    {
        auto replicas = DataService::instance()->lookup_all_remote_idx(full_name);
        if (!replicas.empty()) {
            const auto& best = replicas.front();
            task_dependency_locations_.update(merge_task_id,
                [&](CMUnorderedMap<CMString, CachedLocation>& inner) {
                    inner[full_name] = CachedLocation{best.worker_id_, best.host_, best.port_};
                });
        }
    }

    // Problem 4：merge task 的 assign_task 纳入 schedule_mutex_（同 on_backup_request），
    // 与 scheduler 的 assign 序列互斥，避免 worker_manager 撕裂。send_merge_task 的调用方
    // （merge_db API 流）不持有 schedule_mutex_，无递归死锁。
    {
        std::lock_guard<std::mutex> lk(schedule_mutex_);
        worker_manager_->assign_task(target_worker_id, merge_task_id);
    }

    TaskAssignMessage assign;
    assign.task_id_ = merge_task_id;
    assign.task_name_ = "__merge_object";
    assign.task_module_ = "__fly_internal";
    // args: [short_name, source_db_path, target_db_path, target_data_path, source_host]
    assign.args_ = {short_name, source_db_path, target_db_path, target_data_path, source_host};
    // Part D: merge 产出新对象（新 write context），不再继承源 provenance hash。
    // assign.write_context_hash_ 留空，worker 执行 merge task 时 commit_write guard 填时间戳。
    // （源 db freeze 后 provenance 已被 Part C 清理，继承也无源可取。）

    uint64_t target_conn = lookup_worker_conn(target_worker_id);
    if (target_conn == 0) {
        ERR("send_merge_task: target worker_id={} not connected", target_worker_id);
        // 回滚上面的 assign：消息未送达，worker 永远不会回 TaskComplete/Failed，
        // 不回滚则 BUSY 槽永久泄漏。cancel_task_if_assigned 精确匹配 task_id，
        // 不会误恢复 on_disconnect 已标 DEAD 的 worker。
        worker_manager_->cancel_task_if_assigned(target_worker_id, merge_task_id);
        // 条目由本函数开头 emplace，complete 必命中。
        merge_task_states_.complete(merge_task_id, [](MergeTaskState& s) {
            s.completed_ = true;
            s.success_ = false;
            s.error_message_ = "target worker not connected";
        });
        return merge_task_id;
    }
    reactor_->send(target_conn, assign);

    INFO("Merge task assigned: task_id={}, target_worker={}, object={}, target_data_path={}",
         merge_task_id, target_worker_id, full_name, target_data_path);
    if (metrics_db_) {
        metrics_db_->record_event("db", "DB_MERGE_START", target_worker_id, merge_task_id, full_name);
    }
    return merge_task_id;
}

void MasterAgent::on_merge_task_complete(uint64_t task_id, uint64_t worker_id, const CMVector<WrittenObject>& written_objects) {
    if (merge_task_states_.find(task_id) == nullptr) return;  // 非 merge task，忽略
    if (metrics_db_) {
        metrics_db_->record_event("db", "DB_MERGE_DONE", worker_id, task_id, "");
    }
    merge_task_states_.complete(task_id, [&](MergeTaskState& s) {
        s.completed_ = true;
        s.success_ = true;
        s.worker_id_ = worker_id;
        for (const auto& wo : written_objects) {
            s.written_objects_.push_back(wo.object_name_);
        }
    });
}

void MasterAgent::on_merge_task_failed(uint64_t task_id, const CMString& error_message) {
    if (merge_task_states_.find(task_id) == nullptr) return;
    if (metrics_db_) {
        metrics_db_->record_event("db", "DB_MERGE_FAILED", 0, task_id,
                                  error_message.substr(0, 200));
    }
    merge_task_states_.complete(task_id, [&](MergeTaskState& s) {
        s.completed_ = true;
        s.success_ = false;
        s.error_message_ = error_message;
    });
}

bool MasterAgent::wait_merge_tasks_complete(const CMVector<uint64_t>& task_ids,
                                              int64_t timeout_seconds,
                                              CMVector<CMString>* completed_objects,
                                              CMVector<CMString>* failed_objects) {
    // timeout_seconds<=0 = 无限等待（数据规模相关等待禁设超时：merge 的数据量
    // 与集群 IO 速度不可预估，任何正数超时都是规模假设）。等待只被显式失败
    // 信号终结：task 失败上报 / worker 判死联动（settle_pending_for_dead_worker
    // complete 失败）。timeout_seconds>0 仅测试注入用（QA 注错入口）。
    bool all_ok = true;

    for (uint64_t tid : task_ids) {
        // erase_on_timeout=false：超时保留条目，cleanup_after_merge 还要消费
        //（object→worker 映射重建 remote_idx）。
        auto state = merge_task_states_.wait_for(
            tid, std::chrono::seconds(timeout_seconds),
            [](const CMSharedPtr<MergeTaskState>& s) { return s->completed_; },
            /*erase_on_timeout=*/false);
        if (!state) {
            // 超时：本 task 未完成（仅显式传正超时的测试路径）。
            all_ok = false;
            if (failed_objects) {
                failed_objects->push_back("TIMEOUT:merge_task_" + std::to_string(tid));
            }
            continue;
        }
        if (state->success_) {
            if (completed_objects) {
                for (const auto& name : state->written_objects_) {
                    completed_objects->push_back(name);
                }
            }
        } else {
            all_ok = false;
            if (failed_objects) {
                failed_objects->push_back(state->error_message_.empty()
                    ? ("FAILED:merge_task_" + std::to_string(tid))
                    : state->error_message_);
            }
        }
    }

    // 不在此 erase merge_task_states_ —— cleanup_after_merge 需要读取 (object→worker)
    // 精确映射来重建 remote_idx。cleanup 完成后负责清理这批 task 状态。

    return all_ok;
}

void MasterAgent::send_delete_data(uint64_t source_worker_id,
                                    const CMString& db_path,
                                    const CMString& data_path,
                                    const CMVector<CMString>& writer_ids) {
    CMString ack_key = db_path + ":" + std::to_string(source_worker_id);
    {
        auto [existing, inserted] = pending_delete_acks_.insert_if_absent(
            ack_key, CMMakeShared<PendingDeleteData>());
        if (!inserted) {
            // Problem 5：ack_key 已有 pending 条目（首轮未完成 或 已完成未被 wait 消费）。
            // 无条件覆盖会丢失首轮 ack 状态（ack 不会重发）→ 随后 wait_delete_data_acks
            // 永久超时。保留旧条目让首轮 wait 正确完成。
            WARN("[DELETE-DUP] send_delete_data: ack_key={} 已有 pending 条目"
                 "(completed={}, deleted={})，二次触发保留旧条目不重置",
                 ack_key, existing->completed_, existing->deleted_count_);
        }
    }

    DeleteDataMessage msg;
    msg.db_path_ = db_path;
    // data_path：显式传入优先；否则从 master db_instances_ 查（删源在 cleanup 前执行，
    // 此时 Database 仍是源的 data_path）。
    if (!data_path.empty()) {
        msg.data_path_ = data_path;
    } else {
        std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto it = db_instances_.find(db_path);
        if (it != db_instances_.end()) {
            msg.data_path_ = it->second->get_data_path();
        }
    }
    msg.writer_ids_ = writer_ids;

    uint64_t source_conn = lookup_worker_conn(source_worker_id);
    if (source_conn == 0) {
        ERR("send_delete_data: source worker_id={} not connected", source_worker_id);
        // 保持原有语义：无条件 upsert 标失败（与首轮 insert_if_absent 的保留
        // 语义不一致是既有行为，迁移不改变）。
        pending_delete_acks_.emplace(ack_key, CMMakeShared<PendingDeleteData>());
        pending_delete_acks_.complete(ack_key, [](PendingDeleteData& p) {
            p.completed_ = true;
            p.success_ = false;
            p.error_message_ = "source worker not connected";
        });
        return;
    }
    reactor_->send(source_conn, msg);
    INFO("DeleteData sent: worker_id={}, db_path={}, data_path={}, writer_ids_count={}",
         source_worker_id, db_path, data_path, writer_ids.size());
}

bool MasterAgent::wait_delete_data_acks(const CMVector<uint64_t>& source_worker_ids,
                                          const CMString& db_path,
                                          int64_t timeout_seconds,
                                          CMVector<uint64_t>* failed_workers) {
    // timeout_seconds<=0 = 无限等待（删源数据量不可预估，任何正数超时都是
    // 规模假设）。等待只被显式失败信号终结：ack success_=false / worker 判死
    // 联动（settle_pending_for_dead_worker complete 失败）。
    bool all_ok = true;

    for (uint64_t src_wid : source_worker_ids) {
        CMString ack_key = db_path + ":" + std::to_string(src_wid);
        // erase_on_timeout=false：超时保留条目，由本函数末尾统一清理（merge 语义）。
        auto result = pending_delete_acks_.wait_for(
            ack_key, std::chrono::seconds(timeout_seconds),
            [](const CMSharedPtr<PendingDeleteData>& p) { return p->completed_; },
            /*erase_on_timeout=*/false);
        if (!result) {
            // 超时：本 worker 的 ack 未返回（仅显式传正超时的测试路径）。
            all_ok = false;
            if (failed_workers) failed_workers->push_back(src_wid);
            WARN("wait_delete_data_acks: timeout for worker_id={}, db_path={}", src_wid, db_path);
            continue;
        }
        if (!result->success_) {
            all_ok = false;
            if (failed_workers) failed_workers->push_back(src_wid);
            WARN("wait_delete_data_acks: worker_id={} delete failed: {}",
                 src_wid, result->error_message_);
        }
    }

    // 清理已处理的 ack 状态（防内存泄漏 —— 此前 on_delete_data_ack 只标 completed 不 erase）。
    pending_delete_acks_.with_lock([&](auto& m) {
        for (uint64_t src_wid : source_worker_ids) {
            m.erase(db_path + ":" + std::to_string(src_wid));
        }
    });

    return all_ok;
}

void MasterAgent::on_delete_data_ack(uint64_t conn_id, const DeleteDataAckMessage& msg) {
    // worker_id_ 在 ack 里带回；用它和 db_path 组成 key 找到 pending 项。
    CMString ack_key = msg.db_path_ + ":" + std::to_string(msg.worker_id_);
    if (pending_delete_acks_.find(ack_key) == nullptr) {
        DBG("DeleteDataAck for unknown key={}, ignoring", ack_key);
        return;
    }
    pending_delete_acks_.complete(ack_key, [&](PendingDeleteData& p) {
        p.completed_ = true;
        p.success_ = msg.success_;
        p.deleted_count_ = msg.deleted_count_;
        p.error_message_ = msg.error_message_;
    });
    INFO("DeleteDataAck: worker_id={}, db_path={}, success={}, deleted={}",
         msg.worker_id_, msg.db_path_, msg.success_, msg.deleted_count_);
}

void MasterAgent::cleanup_failed_merge(const CMString& db_path,
                                         const CMString& merge_db_path,
                                         const CMString& merge_data_path) {
    // 1. 按 db 精确清 merge task 状态（不碰其它 db 的并发 merge 条目；
    //    旧实现靠"下一次任意 merge 的全局 completed_ 扫描"兜底，跨 db 误清/漏清）。
    size_t removed = 0;
    merge_task_states_.with_lock([&](auto& m) {
        for (auto it = m.begin(); it != m.end(); ) {
            if (it->second->db_path_ == db_path) {
                it = m.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
    });
    INFO("cleanup_failed_merge: removed {} merge task state(s) for db_path={}",
         removed, db_path);

    // 2. 广播 purge（best-effort，不登记屏障不等 ack）：源命名空间全保留
    //   （源数据支撑重 merge）；持有 target_data_path merge writer 的 worker
    //   删除自己的产物 .dat/.idx（自判，无需 exempt 列表）。
    MergeCleanupMessage msg;
    msg.db_path_ = db_path;
    msg.data_path_ = merge_data_path;
    msg.target_db_path_ = merge_db_path;
    msg.purge_target_ = true;
    for (const auto& [wid, cid] : snapshot_worker_conns()) {
        (void)wid;
        reactor_->send(cid, msg);
    }
    WARN("cleanup_failed_merge: purge broadcast for db_path={} (source data preserved "
         "for re-merge)", db_path);
}

void MasterAgent::cleanup_after_merge(const CMString& db_path,
                                       const CMVector<CMString>& merged_object_full_names,
                                       const CMVector<uint64_t>& source_worker_ids,
                                       const CMVector<uint64_t>& merge_target_worker_ids,
                                       const CMString& merge_db_path,
                                       const CMString& merge_data_path) {
    auto ds = DataService::instance();

    // 1. 登记本轮 cleanup 的 pending（期望 ack 数 = 当前在线 worker 数）。
    //    所有 worker（含 exempt 的 merge target）收到 MergeCleanup 后都回 ack：
    //    exempt worker 不清理但立即回 ack；非 exempt worker 清理完再回 ack。
    //    master 必须收齐全部 ack 才能保证全局状态一致（merge_db 返回前的屏障）。
    uint64_t expected_acks = 0;
    {
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        expected_acks = worker_to_conn_.size();
    }
    {
        // Problem 5：同 db_path 首轮 cleanup 仍在进行中（received < expected）时保留旧
        // 条目（无条件覆盖会把已收到的 ack 计数清零 → 屏障永完不成 → merge_db 卡死）；
        // 不存在或上轮已完成才登记新屏障。
        auto existing = pending_merge_cleanups_.find(db_path);
        if (existing && existing->received_count_ < existing->expected_count_) {
            WARN("[MERGE-CLEANUP-DUP] cleanup_after_merge: db_path={} 首轮未完成"
                 "(received={}/expected={})，二次触发保留旧条目不重置",
                 db_path, existing->received_count_, existing->expected_count_);
        } else {
            pending_merge_cleanups_.emplace(
                db_path, CMMakeShared<PendingMergeCleanup>(expected_acks, 0));
        }
    }

    // 2. 广播 MergeCleanupMessage 给所有 worker：清旧 local_idx/remote_idx，
    //    按新路径 register_database + load 新 idx 重建 local_idx（同 host/共享 FS 可本地直读）。
    //    exempt = merge target workers（已持有效 local_idx，跳过清理但回 ack）。
    MergeCleanupMessage cleanup_msg;
    cleanup_msg.db_path_ = db_path;  // 源 db_path（worker 清旧索引用 + ack 匹配 pending key）
    cleanup_msg.data_path_ = merge_data_path;
    cleanup_msg.target_db_path_ = merge_db_path;  // 产物 db_path（idx 目录）
    cleanup_msg.exempt_worker_ids_ = merge_target_worker_ids;
    for (const auto& [wid, cid] : snapshot_worker_conns()) {
        (void)wid;
        reactor_->send(cid, cleanup_msg);
    }
    INFO("cleanup_after_merge: broadcast MergeCleanup for db_path={} to {} workers "
         "(exempt merge targets: {})", db_path, expected_acks, merge_target_worker_ids.size());

    // 3. 等待所有 worker 回 MergeCleanupAck（全局一致性屏障）。
    //    无限等待（timeout=0）：worker 清理+重载 idx 的耗时随 db 规模增长
    //（数 T 级 db 常见），任何正数超时都是规模假设。等待只被显式信号终结：
    //    全员 ack / worker 判死联动（settle_pending_for_dead_worker 视死亡
    //    worker 为已清理，推进计数）。
    {
        bool all_acked = pending_merge_cleanups_.wait_for(
            db_path, std::chrono::seconds(0),
            [](const CMSharedPtr<PendingMergeCleanup>& p) {
                return p->received_count_ >= p->expected_count_;
            },
            /*erase_on_timeout=*/false) != nullptr;
        if (!all_acked) {
            auto p = pending_merge_cleanups_.find(db_path);
            uint64_t got = p ? p->received_count_ : 0;
            WARN("cleanup_after_merge: MergeCleanupAck barrier not satisfied "
                 "(entry absent): db_path={}, expected={}, received={}",
                 db_path, expected_acks, got);
        }
        pending_merge_cleanups_.erase(db_path);
    }

    // 4. 所有 worker 已清理完毕 → master 重建自身 remote_idx。
    //    此时各 worker 的 local_idx/remote_idx 已是新状态，master 在此后重建不会被覆盖
    //    （worker 不再碰这个 db 的索引）。从 merge_task_states_ 取精确 (object→worker) 映射。
    ds->clear_local_index_for_db(db_path);
    ds->clear_remote_index_for_db(db_path);
    // 跨 path merge 时也清 target 命名空间（merge worker 用 target 落盘/上报）。
    if (merge_db_path != db_path) {
        ds->clear_local_index_for_db(merge_db_path);
        ds->clear_remote_index_for_db(merge_db_path);
    }

    CMUnorderedMap<CMString, CMVector<uint64_t>> obj_to_workers;
    merge_task_states_.with_lock([&](auto& m) {
        for (const auto& [tid, state] : m) {
            // 按 db 过滤：跨 db 的残留条目（含其它 merge 的遗留）不由本次 cleanup
            // 重建——旧实现全表扫描会把陈旧 worker 写进 remote_idx。
            if (state->db_path_ == db_path && state->success_ && state->worker_id_ != 0) {
                for (const auto& obj : state->written_objects_) {
                    obj_to_workers[obj].push_back(state->worker_id_);
                }
            }
        }
    });
    int rebuilt = 0;
    for (const auto& [obj_name, workers] : obj_to_workers) {
        for (uint64_t wid : workers) {
            auto addr = ds->get_worker_address(wid);
            ds->update_remote_idx(obj_name, wid, addr.host_, addr.port_);
        }
        graph_->mark_data_ready(obj_name);
        ++rebuilt;
    }

    // 5. 路径更新（db chain 机制取代 _MIGRATED_TO）。
    //    db_path == 源 db_path。merge_db_path 是产物路径。
    //    set_paths 更新 Database 的内部路径指向 merge 产物。
    //    链更新（target _DB_CHAIN 继承 + 邻居更新 + 彻底删源）由 Python 编排层负责，
    //    不再写 _MIGRATED_TO（机制废弃，由 _DB_CHAIN.absorbed_from + master uid map 取代）。

    // 更新 db_instances_[db_path] 的 Database 路径指向 merge 路径。
    //    Database 是 master 进程路径唯一权威源；set_paths 同步 re-register 进
    //    DataService::db_paths_。db_instances_ 保留源 db_path 作 key（转发锚点），
    //    内部 Database 的 db_path_ 指向 merge 产物。
    //    锁内只做 find→取 shared_ptr / 插入决策；set_paths（re-register）与
    //    Database 构造（重 IO）移出容器锁（D4 拆除，set_paths 自 D1 起自保护）。
    CMSharedPtr<Database> existing_db;
    {
        std::shared_lock<std::shared_mutex> db_lk(db_instances_mutex_);
        auto db_it = db_instances_.find(db_path);
        if (db_it != db_instances_.end()) {
            existing_db = db_it->second;
        }
    }
    if (existing_db) {
        existing_db->set_paths(merge_db_path, merge_data_path);
        // RunSummary：路径已变，作废 freeze 时统计的磁盘值（退出时按新路径补测）。
        if (run_metrics_) run_metrics_->record_db_paths_changed(db_path, merge_data_path);
        if (metrics_db_) {
            metrics_db_->record_event("db", "DB_PATHS_CHANGED", 0, 0,
                                      db_path + " -> " + merge_data_path);
        }
    } else {
        // merge 产物句柄由 Python 经 ex_stg_create_database_with_id 构造，未进 master
        // db_instances_。这里用源 db_path 重建并登记，保证 master 路径权威源与新路径一致。
        // displaced：find-miss 与插入之间若有并发插入，旧实例带出锁外析构。
        auto db = CMMakeShared<Database>(merge_db_path, merge_data_path, 0, "", db_path);
        CMSharedPtr<Database> displaced;
        {
            std::unique_lock<std::shared_mutex> db_lk(db_instances_mutex_);
            auto it = db_instances_.find(db_path);
            if (it != db_instances_.end()) {
                displaced = std::move(it->second);
            }
            db_instances_[db_path] = db;
        }
    }

    // 6. 清理本批 merge task 状态（wait_merge_tasks_complete 推迟到此处 erase）。
    //    按已完成清除（跨并发 merge_db 调用共享此表，completed 的都可安全清理）。
    merge_task_states_.with_lock([&](auto& m) {
        for (auto it = m.begin(); it != m.end(); ) {
            if (it->second->db_path_ == db_path && it->second->completed_) {
                it = m.erase(it);
            } else {
                ++it;
            }
        }
    });

    INFO("cleanup_after_merge: done, db_path={}, rebuilt remote_idx for {} objects (precise worker mapping), "
         "local_idx cleared, db_instances_ path updated to base={} data={}",
         db_path, rebuilt, merge_db_path, merge_data_path);

    // merge 完成后清除源 db_path 的 frozen 状态。源已被合并（delete_source 或
    // path 复用），后续若新建同 path db，WriteRegister 不应被旧 frozen 记录误拒。
    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        frozen_dbs_.erase(db_path);
        pending_frozen_dbs_.erase(db_path);
    }
}

void MasterAgent::on_merge_cleanup_ack(uint64_t conn_id, const MergeCleanupAckMessage& msg) {
    if (pending_merge_cleanups_.find(msg.db_path_) == nullptr) {
        DBG("MergeCleanupAck for unknown db_path={}, ignoring", msg.db_path_);
        return;
    }
    // 持锁 complete：计数器 ++ + notify（原条件 notify 改无条件 notify_all，
    // 未达标的 waiter 醒来重查谓词继续睡，空唤醒无害）。
    pending_merge_cleanups_.complete(msg.db_path_, [&](PendingMergeCleanup& p) {
        p.received_count_++;
        DBG("MergeCleanupAck: db_path={}, worker_id={}, received={}/{}",
            msg.db_path_, msg.worker_id_, p.received_count_, p.expected_count_);
    });
}

void MasterAgent::on_log_message(uint64_t conn_id, const LogMessage& msg) {
    // master 收到 worker 推送的高价值 message：用 master 独立的打印配额控制是否打印
    // （MessageSink::handle_remote 内部 print_within_limit），不记触发次数——
    // 触发发生在 worker，已由 worker 的 MessageRegistry 记录，避免 summary 双算。
    (void)conn_id;
    MessageSink::instance()->handle_remote(msg.worker_id_, msg.level_, msg.domain_id_, msg.source_, msg.msg_);
}

void MasterAgent::on_message_count_report(uint64_t conn_id, const MessageCountReportMessage& msg) {
    // summary 屏障：收集一个 worker 上报的两套计数，凑齐 expected 后唤醒。
    MessageCounts counts;
    auto n = msg.id_keys_.size();
    for (size_t i = 0; i < n; ++i) {
        counts.id_counts_[msg.id_keys_[i]] = msg.id_values_[i];
    }
    auto dn = msg.domain_keys_.size();
    for (size_t i = 0; i < dn; ++i) {
        counts.domain_counts_[msg.domain_keys_[i]] = msg.domain_values_[i];
    }

    std::lock_guard<std::mutex> lk(msg_count_mutex_);
    collected_msg_counts_.push_back(std::make_pair(msg.worker_id_, std::move(counts)));
    pending_msg_count_.received_count_++;
    if (pending_msg_count_.received_count_ >= pending_msg_count_.expected_count_) {
        msg_count_cv_.notify_all();
    }
}

void MasterAgent::collect_and_print_message_summary() {
    // master stop() 在发 ShutdownMessage 之前调用：广播 MSG_COUNT_REQUEST → 等所有 worker
    // 上报（复刻 MergeCleanupAck 屏障，30s 超时容错）→ 合并 master 自身 + 各 worker 计数打印 summary。
    // 死连接（对端已崩溃/FIN 未被处理、或测试注入的 fake conn）不计入 expected、
    // 不发送——等一个永远不会上报的死连接只会白等满 30s 容错。
    CMVector<std::pair<uint64_t, uint64_t>> live_conns;
    {
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
            if (reactor_ && reactor_->is_connected(cid)) {
                live_conns.push_back({wid, cid});
            }
        }
    }
    uint64_t expected = live_conns.size();

    if (expected == 0) {
        // 无存活 worker（单进程模式/全为死连接）：仅用 master 自身计数打印 summary。
        MessageCounts master_counts;
        master_counts.id_counts_ = fly::MessageRegistry::instance().trigger_id_counts_snapshot();
        master_counts.domain_counts_ = fly::MessageRegistry::instance().trigger_domain_counts_snapshot();
        MessageSink::instance()->print_summary(master_counts, {});
        return;
    }

    // 登记本轮 pending 并广播请求。
    {
        std::lock_guard<std::mutex> lk(msg_count_mutex_);
        pending_msg_count_ = {expected, 0};
        collected_msg_counts_.clear();
    }
    MessageCountRequestMessage req;
    for (const auto& [wid, cid] : live_conns) {
        DBG("[SUMMARY] sending MSG_COUNT_REQUEST to worker {} conn {}", wid, cid);
        reactor_->send(cid, req);
    }

    // 等所有 worker 上报（30s 超时容错）。
    // on_disconnect 会在 worker 断开时减少 expected_count 并唤醒 CV。
    {
        std::unique_lock<std::mutex> lk(msg_count_mutex_);
        bool all_reported = msg_count_cv_.wait_for(lk, std::chrono::seconds(30),
            [this] { return pending_msg_count_.received_count_ >= pending_msg_count_.expected_count_; });
        if (!all_reported) {
            WARN("Message summary timeout (30s), {}/{} workers reported",
                 pending_msg_count_.received_count_, pending_msg_count_.expected_count_);
        }
        auto reports = collected_msg_counts_;  // 拷贝出来，避免持锁调用 print_summary
        lk.unlock();

        MessageCounts master_counts;
        master_counts.id_counts_ = fly::MessageRegistry::instance().trigger_id_counts_snapshot();
        master_counts.domain_counts_ = fly::MessageRegistry::instance().trigger_domain_counts_snapshot();
        MessageSink::instance()->print_summary(master_counts, reports);
    }
}

#ifdef FLY_ENABLE_TEST_HOOKS
// ── 测试专用接口（仅 FLY_ENABLE_TEST_HOOKS 编译时存在；release 不定义该宏）──
void MasterAgent::register_fake_worker_for_testing(uint64_t worker_id, uint64_t fake_conn_id) {
    // worker_manager 登记为 IDLE（含 hostname/ip，供 select_best_worker/get_worker_address 使用）；
    // workers map 注入 fake conn 映射，使 assign_task_to_worker 能取到 conn_id。
    // reactor_->send(fake_conn_id) 走 transport 对未知 conn_id 的安全 -1 分支（不崩溃、不触达真实 worker）。
    worker_manager_->register_worker(worker_id, "127.0.0.1", 0, {}, "fake_host", "127.0.0.1");
    std::lock_guard<std::mutex> lk(workers_mutex_);
    conn_to_worker_[fake_conn_id] = worker_id;
    worker_to_conn_[worker_id] = fake_conn_id;
}

void MasterAgent::unregister_fake_worker_for_testing(uint64_t worker_id, uint64_t fake_conn_id) {
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        conn_to_worker_.erase(fake_conn_id);
        worker_to_conn_.erase(worker_id);
    }
    worker_manager_->unregister_worker(worker_id);
    workers_drained_cv_.notify_all();  // 以防 stop() drain 正在等待 worker 断连
}

std::pair<bool, int32_t> MasterAgent::pending_delete_ack_state_for_testing(
        const CMString& ack_key) const {
    auto p = pending_delete_acks_.find(ack_key);
    if (!p) {
        return {false, 0};
    }
    return {p->completed_, p->deleted_count_};
}

size_t MasterAgent::merge_task_state_count_for_testing(const CMString& db_path) const {
    return merge_task_states_.with_lock([&](const auto& m) {
        size_t n = 0;
        for (const auto& [tid, state] : m) {
            if (state->db_path_ == db_path) ++n;
        }
        return n;
    });
}

std::pair<uint64_t, uint64_t> MasterAgent::pending_merge_cleanup_counts_for_testing(
        const CMString& db_path) const {
    auto p = pending_merge_cleanups_.find(db_path);
    if (!p) {
        return {0, 0};
    }
    return {p->expected_count_, p->received_count_};
}

MasterAgent::ObjectBackupScore MasterAgent::backup_score_for_testing(const CMString& object_name) const {
    return backup_scores_.find(object_name).value_or(ObjectBackupScore{});
}
#endif

}  // namespace fly
