#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <message/cpp/message_macros.h>
#include <message/cpp/message_registry.h>
#include <core/cpp/config.h>
#include <core/cpp/system_info.h>
#include <sstream>
#include <core/cpp/process_info.h>
#include <core/cpp/graceful_exit.h>
#include <storage/cpp/local_index.h>
#include <algorithm>
#include <unordered_set>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fly {

std::atomic<uint64_t> MasterAgent::remote_task_counter_{100000};

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), running_(false),
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
    fatal_error_ = false;

    graph_ = CMMakeUnique<DependencyGraph>();
    worker_manager_ = CMMakeUnique<WorkerManager>();

    INFO("MasterAgent start() called, listening on {}:{}", host_, port_);

    auto transport = create_connection_manager("tcp");
    transport->listen(host_, port_);

    reactor_ = CMMakeUnique<Reactor>(std::move(transport));

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
    fly::MessageRegistry::instance().register_id("FLY::0001", fly::LogLevel::INFO);

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
            spec.priority_ = msg.priority_;
            submit_task(task_id, spec);
        });

    reactor_->register_handler<DbPathRequestMessage>(
        [this](uint64_t conn_id, const DbPathRequestMessage& msg) {
            INFO("DbPathRequest received: db_path={}", msg.db_path_);

            DbPathResponseMessage response;
            response.db_path_ = msg.db_path_;

            // 路径权威源收敛到 db_instances_（Database 内嵌 db_path_/data_path_）。
            auto it = db_instances_.find(msg.db_path_);
            if (it != db_instances_.end()) {
                response.db_path_ = it->second->get_db_path();
                response.data_path_ = it->second->get_data_path();
                response.success_ = true;
            } else {
                response.db_path_ = "";
                response.data_path_ = "";
                response.success_ = false;
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

    reactor_->register_handler<ObjectRemovedMessage>(
        [this](uint64_t conn_id, const ObjectRemovedMessage& msg) {
            on_object_removed(conn_id, msg);
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

    reactor_->register_handler<IdxLoadAckMessage>(
        [this](uint64_t conn_id, const IdxLoadAckMessage& msg) {
            on_idx_load_ack(conn_id, msg);
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

    heartbeat_check_running_ = true;
    heartbeat_check_thread_ = std::thread([this] { heartbeat_check_loop(); });

    attr_timeout_check_running_ = true;
    attr_timeout_check_thread_ = std::thread([this] { attr_timeout_check_loop(); });

    reactor_thread_ = std::thread([this] {
        reactor_->run();
        if (drain_thread_.joinable()) {
            drain_thread_.join();
        }
        DataService::instance()->stop_data_server();
        reactor_.reset();
    });
    reactor_->wait_until_running();
    running_ = true;

    auto dsInst = DataService::instance();
    int data_server_threads = static_cast<int>(Config::instance()->get_int("data_server_threads"));
    dsInst->start_data_server(host_, 0, data_server_threads);
    data_server_port_ = static_cast<int32_t>(dsInst->get_data_port());
    DataService::instance()->register_worker(0, host_, data_server_port_);

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

    sigterm_received_ = false;

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
        this->stop();
        fly::Logger::shutdown();
    });
}

void MasterAgent::stop() {
    if (draining_.exchange(true)) return;
    if (!running_) {
        do_drain_and_stop();
        return;
    }

    INFO("MasterAgent stop() called, entering drain phase");

    // Phase 1: Wait for all running tasks to complete (workers are still alive).
    {
        std::unique_lock<std::mutex> lock(drain_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (true) {
            int running_count = metadata_->count_tasks_by_status(TaskStatus::RUNNING);
            if (running_count == 0) break;
            INFO("Drain: waiting for {} running tasks to complete", running_count);
            if (drain_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                WARN("Drain timeout (30s), {} tasks still running", running_count);
                break;
            }
        }
    }

    INFO("Drain: all tasks completed, shutting down workers");

    // Message summary：发 Shutdown 前收集各 worker 的 message 触发计数并打印 summary。
    // 必须在 worker 仍连接时广播请求（worker 断开后无法上报）。
    collect_and_print_message_summary();

    // Phase 2: All tasks done — now send shutdown to workers.
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            INFO("Sending shutdown to worker_id={}", worker_id);
            reactor_->send(conn_id, ShutdownMessage{});
        }
    }

    // Phase 3: Wait for workers to disconnect (reactor will call on_disconnect).
    // Use a simple CV wait — on_disconnect notifies workers_drained_cv_.
    {
        std::unique_lock<std::mutex> lock(workers_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!worker_to_conn_.empty()) {
            if (workers_drained_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                WARN("Shutdown timeout (10s), {} workers still connected", worker_to_conn_.size());
                break;
            }
        }
    }

    persist_pending_tasks();
    do_drain_and_stop();
}

void MasterAgent::do_drain_and_stop() {
    INFO("MasterAgent performing full cleanup");
    // 流程 message：master drain 完成（与 FLY::0000 启动信息对称的关闭里程碑）。
    MSG("FLY::0001", 1, "master drain complete, shutting down");

    shutdown_requested_ = true;

    if (heartbeat_check_thread_.joinable()) {
        heartbeat_check_running_ = false;
        heartbeat_check_cv_.notify_all();
        heartbeat_check_thread_.join();
    }

    if (attr_timeout_check_thread_.joinable()) {
        attr_timeout_check_running_ = false;
        attr_timeout_check_cv_.notify_all();
        attr_timeout_check_thread_.join();
    }

    if (reactor_) {
        DataService::instance()->stop_data_server();

        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
    }

    db_instances_.clear();

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        conn_to_worker_.clear();
        worker_to_conn_.clear();
    }

    running_ = false;
}

bool MasterAgent::is_running() const {
    return running_;
}

void MasterAgent::submit_task(uint64_t task_id, const TaskSubmissionSpec& spec) {
    INFO("submit_task: id={}, name={}, attr_timeout={}, vars={}",
         task_id, spec.name_, spec.attribute_timeout_, spec.vars_.size());

    // Var existence check (advisory only — does not affect scheduling).
    // vars are FULL names (db_path:short_name); split each to locate the Database.
    if (!spec.vars_.empty()) {
        for (const auto& full_var : spec.vars_) {
            auto [db_path, short_name] = split_full_name(full_var);
            if (db_path.empty()) continue;
            auto db_it = db_instances_.find(db_path);
            if (db_it != db_instances_.end() && !db_it->second->master_has_var(short_name)) {
                WARN("task {} declares var '{}' but it does not exist on master (db={})",
                     task_id, short_name, db_path);
            }
        }
    }

    metadata_->create_task(task_id, spec);

    TaskRequirements reqs;
    reqs.capabilities_ = spec.required_capabilities_;
    reqs.timeout_seconds_ = spec.attribute_timeout_;
    reqs.priority_ = spec.priority_;
    graph_->add_task(task_id, spec.inputs_, reqs);

    // Pre-fetch dependency locations at submit time (earliest possible point).
    {
        auto ds = DataService::instance();
        std::lock_guard<std::mutex> lock(dep_loc_mutex_);
        for (const auto& dep : spec.inputs_) {
            auto loc = ds->lookup_remote_idx(dep);
            if (loc.worker_id_ != 0 && !loc.host_.empty()) {
                task_dependency_locations_[task_id][dep] = {loc.worker_id_, loc.host_, loc.port_};
                DBG("[DEP-LOC] submit-time: task={} obj={} worker={}", task_id, dep, loc.worker_id_);
            }
        }
    }

    {
        bool is_ready = graph_->is_task_ready(task_id);
        auto pending = graph_->get_pending_tasks();
        auto ready = graph_->get_ready_tasks();
        auto deps = graph_->get_task_dependencies(task_id);
        DBG("[DEP] submit: id={} name={} deps={} ready={} pending={} is_ready={}",
             task_id, spec.name_, deps.size(), ready.size(), pending.size(), is_ready);
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
                               int priority) {
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
    submit_task(task_id, spec);
}

void MasterAgent::schedule_tasks() {
    if (draining_.load()) return;

    // 每次调度前从 Config 同步开关，使运行时 set_int("locality_scheduling_enabled")
    // 即时生效（无需重启进程）。scheduler 启用后消费 master 预计算的 locality_hint_ 算分。
    bool locality_on =
        Config::instance()->get_int("locality_scheduling_enabled") == 1;

    // locality 预计算在 schedule_mutex_ 锁外执行：DataService 查询（get_remote_size/
    // get_remote_workers）自带锁，不依赖 schedule_mutex_。锁外算好 hint 后，持锁注入
    // graph + schedule_all_available，缩短 schedule_mutex_ 持锁时间（reactor 线程与
    // attr-tick 线程的竞争窗口）。
    // hint 略陈旧无害：remote_idx 更新会再次触发 schedule_tasks()，预计算重做；
    // set_task_locality_hint 对已不存在的 task 静默忽略，新就绪 task 下次补算。
    if (locality_on) {
        auto ready_snapshot = graph_->get_ready_tasks();
        if (!ready_snapshot.empty()) {
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
    }

    std::lock_guard<std::mutex> lock(schedule_mutex_);
    auto ready = graph_->get_ready_tasks();
    auto idle = worker_manager_->get_idle_workers();

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

    // 依赖不可解检测：上游 task 失败导致数据被清理后，依赖该数据的 pending
    // task 永远无法就绪。此时若 ready 空（无 task 可调度）且无 running task
    // （无 task 可能产出该数据），应立即判定这些 pending task 失败，而非空等。
    // 这与属性死锁（fail_unscheduleable_tasks 开关控制）是不同的失败模式，
    // 不受该开关影响 —— 数据依赖丢失是确定性的，应即时失败并持久化供 restart。
    {
        auto pending = graph_->get_pending_tasks();
        if (!pending.empty()) {
            auto ready = graph_->get_ready_tasks();
            if (ready.empty() && !metadata_->has_tasks_with_status(TaskStatus::RUNNING)) {
                for (uint64_t task_id : pending) {
                    auto deps = graph_->get_task_dependencies(task_id);
                    CMString dep_list;
                    for (size_t i = 0; i < deps.size(); i++) {
                        if (i > 0) dep_list += ",";
                        dep_list += deps[i];
                    }
                    CMString error_msg = "Unresolvable data dependencies: [" + dep_list + "]";

                    FailedTaskRecord record = make_failed_record(task_id, error_msg);

                    graph_->remove_task(task_id);
                    metadata_->fail_task(task_id, error_msg);

                    persist_failed_task(record);
                    ERR("Task {} failed: {}", task_id, error_msg);
                }
            }
        }
    }

    if (Config::instance()->get_int("fail_unscheduleable_tasks") != 1) return;

    auto remaining = graph_->get_ready_tasks();
    // 属性死锁检测：仅当集群中无任何 worker（含 BUSY）具备所需属性，且无 running
    // task（即没有 worker 可能通过 set_worker_property 动态获得属性）时，
    // 才 fail 掉死等(timeout<0)的 task。限时(>=0)的 task 会被降级调度，不在此处 fail。
    bool has_running = metadata_->has_tasks_with_status(TaskStatus::RUNNING);
    for (uint64_t task_id : remaining) {
        const auto& requirements = graph_->get_task_requirements(task_id);
        if (requirements.capabilities_.empty()) continue;
        // 仅死等(timeout<0)的 task 才适用属性死锁 fail；timeout>=0 的会被降级调度
        if (requirements.timeout_seconds_ >= 0.0f) continue;
        // 有 running task 时，worker 仍可能动态获得属性，不能判定死锁
        if (has_running) continue;

        if (!worker_manager_->has_worker_with_all_capabilities(requirements.capabilities_)) {
            CMString cap_list;
            for (size_t i = 0; i < requirements.capabilities_.size(); i++) {
                if (i > 0) cap_list += ",";
                cap_list += requirements.capabilities_[i];
            }
            CMString error_msg = "No worker with required capabilities: [" + cap_list + "]";

            FailedTaskRecord record = make_failed_record(task_id, error_msg);

            graph_->remove_task(task_id);
            metadata_->fail_task(task_id, error_msg);

            persist_failed_task(record);
            ERR("Task {} failed: {}", task_id, error_msg);
        }
    }
}

void MasterAgent::update_dependency_location_cache(const CMString& object_name, uint64_t worker_id, const CMString& host, int32_t port) {
    // Find pending tasks that depend on this data and cache the location.
    auto pending = graph_->get_pending_tasks();
    std::lock_guard<std::mutex> lock(dep_loc_mutex_);
    for (uint64_t task_id : pending) {
        auto deps = graph_->get_task_dependencies(task_id);
        for (const auto& dep : deps) {
            if (dep == object_name) {
                task_dependency_locations_[task_id][object_name] = {worker_id, host, port};
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
            auto db_it = db_instances_.find(db_path);
            if (db_it == db_instances_.end()) continue;
            auto [found, value, type_name] = db_it->second->master_get_var(short_name);
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
            std::lock_guard<std::mutex> lock(dep_loc_mutex_);
            auto loc_it = task_dependency_locations_.find(task_id);
            if (loc_it != task_dependency_locations_.end()) {
                for (const auto& [dep, loc] : loc_it->second) {
                    msg.dependency_locations_.push_back({dep, loc.worker_id, loc.host, loc.port});
                }
                task_dependency_locations_.erase(loc_it);  // Consume cache.
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
                auto loc = ds->lookup_remote_idx(dep);
                if (loc.worker_id_ != 0 && !loc.host_.empty()) {
                    msg.dependency_locations_.push_back({dep, loc.worker_id_, loc.host_, loc.port_});
                }
            }
        }
    }

    reactor_->send(conn_id, msg);

    metadata_->assign_task(task_id, worker_id);
    worker_manager_->assign_task(worker_id, task_id);
}

void MasterAgent::heartbeat_check_loop() {
    while (heartbeat_check_running_) {
        {
            std::unique_lock<std::mutex> lock(heartbeat_check_mutex_);
            heartbeat_check_cv_.wait_for(lock, std::chrono::seconds(5),
                                          [this]{ return !heartbeat_check_running_.load(); });
        }

        if (running_ && !draining_.load()) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

            heartbeat_monitor_->check_all_workers(timestamp);

            auto dead = heartbeat_monitor_->get_dead_workers();
            for (uint64_t worker_id : dead) {
                WARN("worker timeout: {}", worker_id);

                std::lock_guard<std::mutex> lk(workers_mutex_);
                auto conn_it = worker_to_conn_.find(worker_id);
                if (conn_it != worker_to_conn_.end()) {
                    ShutdownMessage shutdown;
                    reactor_->send(conn_it->second, shutdown);
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

void MasterAgent::on_worker_register(uint64_t conn_id, const RegisterMessage& msg) {
    uint64_t worker_id = msg.worker_id_;

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        conn_to_worker_[conn_id] = worker_id;
        worker_to_conn_[worker_id] = conn_id;
    }

    worker_manager_->register_worker(worker_id, host_, port_, msg.attributes_,
                                      msg.hostname_, msg.ip_address_);

    DataService::instance();
    if (msg.data_server_port_ > 0) {
        DataService::instance()->register_worker(worker_id, msg.data_server_host_, msg.data_server_port_);
        INFO("Worker registered: worker_id={}, conn_id={}, hostname={}, data_server={}:{}",
             worker_id, conn_id, msg.hostname_, msg.data_server_host_, msg.data_server_port_);
    }

    RegisterAckMessage ack;
    ack.worker_id_ = worker_id;
    ack.master_address_ = host_;
    ack.master_port_ = static_cast<int32_t>(port_);
    reactor_->send(conn_id, ack);

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
    if (worker && worker->get().status_ == WorkerStatus::DEAD) {
        worker_manager_->update_worker_status(worker_id, WorkerStatus::IDLE);
        INFO("Worker {} revived (heartbeat received after timeout)", worker_id);
    }

    HeartbeatAckMessage ack;
    ack.worker_id_ = worker_id;
    reactor_->send(conn_id, ack);

    DBG("Heartbeat from worker_id={}", worker_id);
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
        auto db_it2 = db_instances_.find(db_path);
        if (db_it2 != db_instances_.end()) {
            writer_id = db_it2->second->get_writer_id();
        }
    }

    auto key = std::make_tuple(db_path, hostname, writer_id);
    {
        std::lock_guard<std::mutex> lk(recorded_workers_mutex_);
        if (recorded_workers_.find(key) == recorded_workers_.end()) {
            recorded_workers_.insert(key);
            auto db_it = db_instances_.find(db_path);
            if (db_it != db_instances_.end()) {
                ::WorkerInfo info;
                info.worker_id_ = worker_id;
                info.writer_id_ = writer_id;
                info.hostname_ = hostname;
                info.ip_address_ = ip;
                info.launch_command_ = "";
                db_it->second->append_worker_info_to_meta(info);
            }
        }
    }
}

// master 自写对象（worker_id==0）的 auto-backup 评估。从原 on_data_ready 抽出。
void MasterAgent::evaluate_and_trigger_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_path) {
    auto target_replicas = static_cast<uint32_t>(Config::instance()->get_int("backup_replicas"));
    auto decision = DataService::instance()->evaluate_auto_backup(object_name, source_worker_id, target_replicas);
    if (decision.should_backup_) {
        trigger_auto_backup(object_name, source_worker_id, db_path);
    }
}

void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    size_t written_count = msg.written_objects_.size();
    INFO("Task complete: task_id={}, written_objects={}", msg.task_id_, written_count);

    uint64_t worker_id = msg.worker_id_;

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
                    record_worker_info(wo.object_name_, db_path, worker_id, "");
                }
                DBG("Recorded data location (non-stream, task complete): {} -> worker {}", wo.object_name_, worker_id);
            }
        }

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
        // task 的 module/args/vars 随 TaskMetadata.submission_ 存活，状态迁移到
        // COMPLETED 即完成生命周期管理，无需单独清理并行 map。
    } else {
        // Internal tasks (backup, etc.) always update remote_idx
        for (const auto& wo : msg.written_objects_) {
            DataService::instance()->update_remote_idx(wo.object_name_, worker_id, addr.host_, addr.port_, wo.size_bytes_);
            DBG("Internal task: recorded data location: {} -> worker {}", wo.object_name_, worker_id);
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

    worker_manager_->complete_task(worker_id);
    metadata_->fail_task(msg.task_id_, msg.error_message_);
    graph_->remove_task(msg.task_id_);

    // 运行时失败的 task（异常/读不到数据）也应可 restart，与调度时失败的 task 一致。
    // make_failed_record 从 metadata.submission_ 整体拷贝，无需逐字段复制。
    {
        FailedTaskRecord record = make_failed_record(msg.task_id_, msg.error_message_);
        persist_failed_task(record);
    }

    // 清理失败 task 已写出的脏对象：worker 已本地撤销（idx ABORT + data truncate），
    // master 据此清理 remote_idx / provenance / 依赖图，并广播 OBJECT_REMOVED
    // 通知其他 worker 清缓存，避免读到失效数据。
    for (const auto& obj : msg.dirty_objects_) {
        DataService::instance()->remove_remote_index(obj);
        {
            std::lock_guard<std::mutex> lk(provenance_mutex_);
            write_provenance_.erase(obj);
        }
        graph_->mark_data_removed(obj);

        auto [db_path, short_name] = fly::split_full_name(obj);
        if (!db_path.empty()) {
            broadcast_object_removed(db_path, short_name);
        }
        WARN("Dirty object cleaned after task failure: task_id={}, object={}",
             msg.task_id_, obj);
    }

    if (msg.error_type_ == TaskErrorType::WRITE_REGISTRATION_TIMEOUT ||
        msg.error_type_ == TaskErrorType::EXECUTION_ERROR) {
        fatal_error_ = true;
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
    uint64_t worker_id = 0;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        auto it = conn_to_worker_.find(conn_id);
        if (it != conn_to_worker_.end()) {
            worker_id = it->second;
            conn_to_worker_.erase(conn_id);
            worker_to_conn_.erase(worker_id);
        }
    }
    if (worker_id == 0) return;

    worker_manager_->update_worker_status(worker_id, WorkerStatus::DEAD);

    WARN("Worker disconnected: worker_id={}", worker_id);
    // 流程 message：worker 掉线（非 drain 期才打，drain 期属正常关闭会刷屏）。
    if (!draining_.load()) {
        MSG("AGENT::0002", 1, "worker {} offline", worker_id);
    }

    auto tasks_to_recover = metadata_->get_task_ids_by_worker(worker_id);

    // 崩溃恢复：worker 断连可能意味着进程崩溃（收不到失败消息），必须按 task_id
    // 清掉这些 task 声明的 pending frozen，否则该 db 会被永久标"冻结中" → 后续所有
    // 写被拒 → 死锁级 bug。这是 Q1 选 task_id 而非 db_path 的核心理由。
    for (uint64_t task_id : tasks_to_recover) {
        rollback_pending_frozen(task_id);
    }

    if (draining_.load()) {
        // During shutdown: mark running tasks as FAILED so drain can complete.
        for (uint64_t task_id : tasks_to_recover) {
            metadata_->fail_task(task_id, "Worker disconnected during shutdown");
            graph_->remove_task(task_id);
            WARN("Task failed due to shutdown disconnect: task_id={}", task_id);
        }
        notify_drain_if_active();  // Wake up stop() drain wait.
    } else {
        // Normal operation: re-queue tasks for recovery.
        for (uint64_t task_id : tasks_to_recover) {
            auto task_opt4 = metadata_->get_task(task_id);
            if (!task_opt4) continue;

            const auto& s = task_opt4->submission_;
            graph_->remove_task(task_id);
            TaskRequirements reqs;
            reqs.capabilities_ = s.required_capabilities_;
            reqs.timeout_seconds_ = s.attribute_timeout_;
            reqs.priority_ = s.priority_;
            graph_->add_task(task_id, s.inputs_, reqs);
            metadata_->unassign_task(task_id);
            WARN("Recovered task from dead worker: task_id={}, name={}", task_id, s.name_);
        }

        if (!tasks_to_recover.empty()) {
            schedule_tasks();
        }
    }

    // Notify stop() that a worker has disconnected.
    workers_drained_cv_.notify_one();
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

CMVector<std::pair<uint64_t, CMString>> MasterAgent::get_worker_hostnames() const {
    CMVector<std::pair<uint64_t, CMString>> result;
    // 遍历 WorkerManager（hostname 已收编进 WorkerInfo），取代原并行 map。
    for (const auto& info : worker_manager_->get_all_workers()) {
        result.push_back({info.worker_id_, info.hostname_});
    }
    return result;
}

void MasterAgent::add_worker_hostname(uint64_t worker_id, const CMString& hostname) {
    // 转发到 WorkerManager（hostname 收编进 WorkerInfo）。若 worker 未注册则先注册，
    // 兼容测试在无网络注册流程下直接设置拓扑的场景。
    if (!worker_manager_->get_worker(worker_id)) {
        worker_manager_->register_worker(worker_id, "", 0, {});
    }
    worker_manager_->set_hostname(worker_id, hostname);
}

size_t MasterAgent::get_connection_count() const {
    std::lock_guard<std::mutex> lk(workers_mutex_);
    return conn_to_worker_.size();
}

CMVector<uint64_t> MasterAgent::get_pending_tasks() const {
    return graph_->get_pending_tasks();
}

CMVector<uint64_t> MasterAgent::get_running_tasks() const {
    return metadata_->get_task_ids_by_status(TaskStatus::RUNNING);
}

CMVector<uint64_t> MasterAgent::get_completed_tasks() const {
    return metadata_->get_task_ids_by_status(TaskStatus::COMPLETED);
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
    // Database 是 master 进程路径唯一权威源：构造对象插入 db_instances_，路径内嵌于对象，
    // DataService::db_paths_ 由 Database 构造时自动 register。
    auto db = CMMakeShared<Database>(db_path, data_path, 0, "", db_path);
    db_instances_[db_path] = db;
}

bool MasterAgent::is_db_frozen(const CMString& db_path) const {
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    return frozen_dbs_.count(db_path) > 0 || pending_frozen_dbs_.count(db_path) > 0;
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
        auto it = db_instances_.find(db_path);
        if (it != db_instances_.end()) it->second->freeze();
        INFO("DB frozen (committed by task): db_path={}, task_id={}", db_path, task_id);
        DatabaseFreezeNotification broadcast_msg;
        broadcast_msg.db_path_ = db_path;
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
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
    auto db = CMMakeShared<Database>(db_path, data_path, writer_id);
    db_instances_[db_path] = db;
    return db;
}

CMSharedPtr<Database> MasterAgent::get_database(const CMString& db_path) const {
    auto it = db_instances_.find(db_path);
    return it != db_instances_.end() ? it->second : nullptr;
}

CMVector<uint64_t> MasterAgent::get_idle_workers() const {
    return worker_manager_->get_idle_workers();
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
            response.locations_.push_back(std::move(dl));
        }

        if (Config::instance()->get_int("auto_backup_enabled") == 1) {
            DataService::instance()->record_remote_access(msg.object_name_);

            auto threshold = static_cast<uint64_t>(Config::instance()->get_int("backup_threshold"));
            auto target_replicas = static_cast<uint32_t>(Config::instance()->get_int("backup_replicas"));

            auto decision = DataService::instance()->evaluate_auto_backup(msg.object_name_, threshold, target_replicas);
            INFO("[AUTO-BACKUP] obj={}, read_count={}, current_replicas={}, target={}, should_backup={}",
                 msg.object_name_, decision.read_count_, decision.current_replicas_, target_replicas, decision.should_backup_);
            if (decision.should_backup_ && !all_locs.empty()) {
                CMString db_path = msg.object_name_;
                auto colon_pos = msg.object_name_.find(':');
                if (colon_pos != CMString::npos) {
                    db_path = msg.object_name_.substr(0, colon_pos);
                }
                trigger_auto_backup(msg.object_name_, all_locs.front().worker_id_, db_path);
            }
        }
    } else {
        response.success_ = false;
        bool has_pending = !graph_->get_pending_tasks().empty();
        bool has_running = metadata_->has_tasks_with_status(TaskStatus::RUNNING);
        response.can_still_produce_ = has_pending || has_running;
        DBG("[TEMP-QUERY] DataQuery NOT FOUND: obj={}, can_still_produce={}", msg.object_name_, response.can_still_produce_);
    }

    reactor_->send(conn_id, response);
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
    if (is_db_frozen(msg.db_path_)) {
        ack.success_ = false;
        ack.error_message_ = "Database frozen: " + msg.db_path_;
        ack.error_type_ = TaskErrorType::WRITE_TO_FROZEN_DB;
        WARN("WriteRegister rejected: db {} is frozen", msg.db_path_);
    } else if (!msg.write_context_hash_.empty()) {
        std::lock_guard<std::mutex> lk(provenance_mutex_);
        auto it = write_provenance_.find(msg.object_name_);
        if (it == write_provenance_.end()) {
            write_provenance_[msg.object_name_] = msg.write_context_hash_;
            registered_ok = true;
        } else if (it->second == msg.write_context_hash_) {
            registered_ok = true;
        } else {
            ack.success_ = false;
            ack.error_message_ = "Write provenance mismatch for " + msg.object_name_ +
                ": existing hash=" + it->second + " new hash=" + msg.write_context_hash_;
            ack.error_type_ = TaskErrorType::WRITE_PROVENANCE_MISMATCH;
            ERR("WriteRegister rejected: provenance mismatch for {}", msg.object_name_);
        }
    } else {
        registered_ok = true;
    }

    if (registered_ok) {
        ack.success_ = true;

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
            graph_->mark_data_ready(msg.object_name_);
            auto addr = DataService::instance()->get_worker_address(msg.worker_id_);
            DataService::instance()->update_remote_idx(msg.object_name_, msg.worker_id_, addr.host_, addr.port_, msg.size_bytes_);
            update_dependency_location_cache(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
            record_worker_info(msg.object_name_, msg.db_path_, msg.worker_id_, msg.writer_id_);
            if (master_self_write && Config::instance()->get_int("auto_backup_enabled") == 1) {
                evaluate_and_trigger_backup(msg.object_name_, 0, msg.db_path_);
            }
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

    // 属性变化后立即触发调度：worker 通过 set_worker_property 获得新属性后，
    // 等待该属性的 task（waiting 中）应立即被调度，无需等到 timeout。
    schedule_tasks();
}

void MasterAgent::on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg) {
    INFO("ObjectRemoved: object={}, db_path={}", msg.object_name_, msg.db_path_);

    DataService::instance()->remove_remote_index(msg.object_name_);
    {
        std::lock_guard<std::mutex> lk(provenance_mutex_);
        write_provenance_.erase(msg.object_name_);
    }

    ObjectRemovedMessage broadcast_msg = msg;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, worker_conn_id] : worker_to_conn_) {
            if (worker_conn_id != conn_id) {
                reactor_->send(worker_conn_id, broadcast_msg);
            }
        }
    }
}

void MasterAgent::broadcast_object_removed(const CMString& db_path, const CMString& object_name) {
    CMString full = db_path + ":" + object_name;

    DataService::instance()->remove_remote_index(full);
    {
        std::lock_guard<std::mutex> lk(provenance_mutex_);
        write_provenance_.erase(full);
    }

    ObjectRemovedMessage msg;
    msg.object_name_ = full;
    msg.db_path_ = db_path;

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            reactor_->send(conn_id, msg);
        }
    }
}

void MasterAgent::broadcast_message_limits() {
    // 从 master 的 MessageRegistry 取当前所有配额设置（全量快照），广播给所有在线 worker。
    // worker 收到后整体替换本地配额（不清零计数）。支持运行时动态修改：每次 set_*_limit 触发。
    MessageLimitSyncMessage msg;
    fly::MessageRegistry::instance().get_all_limits(
        msg.global_limit_, msg.domain_keys_, msg.domain_values_,
        msg.id_keys_, msg.id_values_);

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            (void)worker_id;
            reactor_->send(conn_id, msg);
        }
    }
}

void MasterAgent::on_var_set(uint64_t conn_id, const VarSetMessage& msg) {
    VarAckMessage ack;
    ack.var_name_ = msg.var_name_;  // echo the full name

    auto [db_path, short_name] = split_full_name(msg.var_name_);
    auto it = db_path.empty() ? db_instances_.end() : db_instances_.find(db_path);
    if (it == db_instances_.end()) {
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

    bool ok = it->second->master_set_var(short_name, buf, msg.type_name_);
    ack.success_ = ok;
    if (!ok) {
        if (it->second->is_frozen()) {
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
    auto it = db_path.empty() ? db_instances_.end() : db_instances_.find(db_path);
    if (it == db_instances_.end()) {
        ack.success_ = false;
        reactor_->send(conn_id, ack);
        return;
    }

    auto [found, value, type_name] = it->second->master_get_var(short_name);
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
        auto it = db_instances_.find(db_path);
        if (it != db_instances_.end()) {
            it->second->master_remove_var(short_name);
            // Broadcast the removal (full name) to all workers so they drop caches.
            broadcast_var(msg.var_name_, false);
        }
    }
}

void MasterAgent::broadcast_var(const CMString& full_var_name, bool is_modification_reject) {
    VarBroadcastMessage msg;
    msg.var_name_ = full_var_name;
    msg.is_modification_reject_ = is_modification_reject;

    std::lock_guard<std::mutex> lk(workers_mutex_);
    for (const auto& [worker_id, conn_id] : worker_to_conn_) {
        reactor_->send(conn_id, msg);
    }
}

void MasterAgent::on_remove_request(uint64_t conn_id, const RemoveRequestMessage& msg) {
    INFO("RemoveRequest: object={}, db_path={}", msg.object_name_, msg.db_path_);

    graph_->mark_data_removed(msg.object_name_);

    auto worker_ids = DataService::instance()->get_remote_workers(msg.object_name_);

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
        cmd.db_path_ = msg.db_path_;
        cmd.object_name_ = msg.object_name_;
        reactor_->send(worker_conn_id, cmd);
        INFO("RemoveCommand sent to worker_id={}: object={}", wid, msg.object_name_);
    }

    DataService::instance()->remove_remote_location(msg.object_name_);

    {
        std::lock_guard<std::mutex> lk(provenance_mutex_);
        write_provenance_.erase(msg.object_name_);
    }

    RemoveAckMessage ack;
    ack.db_path_ = msg.db_path_;
    ack.object_name_ = msg.object_name_;
    ack.success_ = true;
    reactor_->send(conn_id, ack);

    schedule_tasks();

    INFO("RemoveRequest completed: object={}, workers_notified={}", msg.object_name_, worker_ids.size());
}

CMString MasterAgent::get_failed_tasks_file_path() const {
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
    CMString file_path = get_failed_tasks_file_path();
    append_failed_record(file_path, record);

    ERR("Task {} failed and persisted. To restart after fixing, call restart_failed_tasks(\"{}\")", record.task_id_, file_path);
}

void MasterAgent::remove_persisted_task(uint64_t task_id) {
    CMString file_path = get_failed_tasks_file_path();
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

void MasterAgent::restart_failed_tasks(const CMString& file_path) {
    if (!std::filesystem::exists(file_path)) {
        WARN("No failed tasks file found at {}", file_path);
        return;
    }

    auto records = read_failed_records(file_path);
    if (records.empty()) {
        WARN("No failed tasks to restart");
        return;
    }

    size_t record_count = records.size();
    INFO("Restarting {} failed tasks", record_count);

    std::filesystem::remove(file_path);
    INFO("Cleared failed tasks file {}", file_path);

    for (auto& record : records) {
        metadata_->remove_task(record.task_id_);
        // record.submission_ 携带完整的提交字段（含 priority/attribute_timeout/vars），
        // 整体传入 submit_task，消除原先 11 个位置参数的错位/漏传风险。
        submit_task(record.task_id_, record.submission_);
    }

    INFO("Restarted {} failed tasks", record_count);
}

void MasterAgent::setup_write_context() {
    // master 自写对象的 record 阶段无需处理（register 已含全部 placement/schedule 逻辑）。
    // 留一个空 record_write_func 仅满足 is_active() 探测，不触发任何动作。
    WorkerAgentContext::set_record_write_func([](const CMString&, const CMString&, int64_t) {});
    WorkerAgentContext::set_register_func([this](const CMString& db_path, const CMString& name, int64_t compressed_size) -> std::pair<CMString, TaskErrorType> {
        return on_master_register_write(db_path, name, compressed_size);
    });
    WorkerAgentContext::set_freeze_func([this](const CMString& db_path) {
        on_master_freeze(db_path);
    });
    // Var funcs: master process operates directly on the authoritative Database
    // store (no network). The context passes FULL var names (db_path:short_name);
    // split off db_path to locate the Database, then query with the short name.
    WorkerAgentContext::set_set_var_func([this](const CMString& full_var_name,
                                                FlyBufferPtr value, const CMString& type_name) -> bool {
        auto [db_path, short_name] = split_full_name(full_var_name);
        if (db_path.empty()) return false;
        auto it = db_instances_.find(db_path);
        if (it == db_instances_.end()) return false;
        return it->second->master_set_var(short_name, value, type_name);
    });
    WorkerAgentContext::set_get_var_func([this](const CMString& full_var_name)
        -> std::tuple<bool, FlyBufferPtr, CMString> {
        auto [db_path, short_name] = split_full_name(full_var_name);
        if (db_path.empty()) return {false, nullptr, ""};
        auto it = db_instances_.find(db_path);
        if (it == db_instances_.end()) return {false, nullptr, ""};
        return it->second->master_get_var(short_name);
    });
    WorkerAgentContext::set_remove_var_func([this](const CMString& full_var_name) {
        auto [db_path, short_name] = split_full_name(full_var_name);
        if (!db_path.empty()) {
            auto it = db_instances_.find(db_path);
            if (it != db_instances_.end()) {
                it->second->master_remove_var(short_name);
                broadcast_var(full_var_name, false);
            }
        }
    });
}

std::pair<CMString, TaskErrorType> MasterAgent::on_master_register_write(const CMString& db_path, const CMString& name, int64_t compressed_size) {
    if (!running_.load()) return {"", TaskErrorType::UNKNOWN};
    // master 自写走统一的 WriteRegisterMessage 路径（worker_id=0），与 worker 行为对称。
    // 同步调用 do_write_register，丢弃 ack（master 自写无需网络 ACK）。
    WriteRegisterMessage msg;
    msg.worker_id_ = 0;
    msg.object_name_ = db_path + ":" + name;
    msg.db_path_ = db_path;
    msg.size_bytes_ = compressed_size;
    auto db_it = db_instances_.find(db_path);
    if (db_it != db_instances_.end()) {
        msg.writer_id_ = db_it->second->get_writer_id();
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
        for (const auto& entry : entries) {
            graph_->mark_data_ready(db_path + ":" + entry.object_name_);
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

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            reactor_->send(conn_id, msg);
            INFO("Sent IdxLoadCommand to worker_id={}: db_path={}, writer_ids_count={}",
                 worker_id, db_path, writer_ids.size());
        }
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

    std::lock_guard<std::mutex> lk(workers_mutex_);
    auto it = worker_to_conn_.find(worker_id);
    if (it == worker_to_conn_.end()) {
        ERR("send_idx_load_to_worker: worker_id={} not found", worker_id);
        return;
    }
    reactor_->send(it->second, msg);
    INFO("Sent IdxLoadCommand to worker_id={}: db_path={}, writer_ids_count={}",
         worker_id, db_path, writer_ids.size());
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
    }
}

void MasterAgent::on_idx_load_ack(uint64_t conn_id, const IdxLoadAckMessage& msg) {
    INFO("IdxLoadAck: worker_id={}, db_path={}, success={}, loaded_count={}, writer_ids={}",
         msg.worker_id_, msg.db_path_, msg.success_, msg.loaded_count_, msg.loaded_writer_ids_.size());

    if (!msg.success_) {
        ERR("IdxLoadAck failed from worker_id={}: {}", msg.worker_id_, msg.error_message_);
        return;
    }

    // Master reads the same idx files from shared filesystem and updates remote_idx_
    auto it = db_instances_.find(msg.db_path_);
    if (it == db_instances_.end()) {
        ERR("IdxLoadAck: unknown db_path={}", msg.db_path_);
        return;
    }

    rebuild_remote_idx_for_worker(msg.db_path_, msg.loaded_writer_ids_, msg.worker_id_);
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
        // stream 模式的本地 freeze + 广播
        auto it = db_instances_.find(msg.db_path_);
        if (it != db_instances_.end()) {
            it->second->freeze();
        }
        DatabaseFreezeNotification broadcast_msg = msg;
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
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

    DatabaseFreezeNotification msg;
    msg.db_path_ = db_path;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, msg);
        }
    }

    // 流程 message：master 直接 freeze 完成（不可逆里程碑）。
    MSG("STOR::0001", 2, "db {} frozen (master direct)", db_path);
}

std::atomic<bool> MasterAgent::sigterm_received_{false};

void MasterAgent::sigterm_handler(int sig) {
    sigterm_received_ = true;
}

void MasterAgent::check_shutdown_request() {
    if ((sigterm_received_.load() || fatal_error_.load()) && !draining_.load()) {
        if (!shutdown_requested_.exchange(true)) {
            draining_ = true;
            INFO("Shutdown requested (fatal_error={}, sigterm={}), triggering drain",
                 fatal_error_.load(), sigterm_received_.load());

            {
                std::lock_guard<std::mutex> lk(workers_mutex_);
                for (const auto& [wid, cid] : worker_to_conn_) {
                    reactor_->send(cid, ShutdownMessage{});
                }
            }

            reactor_->stop();
            drain_thread_ = std::thread([this] {
                persist_pending_tasks();
                shutdown_requested_ = true;
                if (heartbeat_check_thread_.joinable()) {
                    heartbeat_check_running_ = false;
                    heartbeat_check_cv_.notify_all();
                    heartbeat_check_thread_.join();
                }
                if (attr_timeout_check_thread_.joinable()) {
                    attr_timeout_check_running_ = false;
                    attr_timeout_check_cv_.notify_all();
                    attr_timeout_check_thread_.join();
                }
                db_instances_.clear();
                {
                    std::lock_guard<std::mutex> lk(workers_mutex_);
                    conn_to_worker_.clear();
                    worker_to_conn_.clear();
                }
                running_ = false;
            });
        }
    }
}

void MasterAgent::notify_drain_if_active() {
    if (draining_.load()) {
        drain_cv_.notify_one();
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

    uint64_t backup_worker_id = select_backup_worker(msg.worker_id_);
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

    worker_manager_->assign_task(backup_worker_id, backup_task_id);

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
        std::lock_guard<std::mutex> lk(provenance_mutex_);
        auto prov_it = write_provenance_.find(msg.object_name_);
        if (prov_it != write_provenance_.end()) {
            assign.write_context_hash_ = prov_it->second;
        }
    }

    reactor_->send(backup_conn, assign);
    INFO("Backup task assigned to worker_id={} for object={}", backup_worker_id, msg.object_name_);
}

uint64_t MasterAgent::select_backup_worker(uint64_t source_worker_id) {
    std::lock_guard<std::mutex> lk(workers_mutex_);

    // hostname + status 统一来自 WorkerInfo（一次遍历，取代原 hostname map 遍历
    // + 逐个 get_worker 查 status 的双源 join）。
    CMString source_hostname = worker_manager_->get_hostname(source_worker_id);

    uint64_t fallback_worker = 0;
    for (const auto& info : worker_manager_->get_all_workers()) {
        if (info.worker_id_ == source_worker_id) continue;
        if (info.worker_id_ == 0) continue;
        if (info.status_ == WorkerStatus::DEAD) continue;

        if (info.hostname_ != source_hostname) {
            return info.worker_id_;
        }
        if (fallback_worker == 0) {
            fallback_worker = info.worker_id_;
        }
    }

    if (fallback_worker != 0) {
        INFO("select_backup_worker: all workers on same host, using worker_id={}", fallback_worker);
    }
    return fallback_worker;
}

void MasterAgent::trigger_auto_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_path) {
    INFO("Auto-backup triggered: object={}, source_worker={}", object_name, source_worker_id);

    BackupRequestMessage backup_msg;
    backup_msg.worker_id_ = source_worker_id;
    backup_msg.object_name_ = object_name;
    backup_msg.db_path_ = db_path;

    on_backup_request(0, backup_msg);
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
    {
        std::lock_guard<std::mutex> lk(merge_task_mutex_);
        merge_task_states_[merge_task_id] = MergeTaskState{};
    }

    CMString full_name = source_db_path + ":" + short_name;
    // 把源对象位置注入 task dependency_locations_，让 target worker 的 read_raw_compressed
    // 直接 TIER2 命中（无需 TIER3 回查 master）。源位置从 remote_idx 取。
    {
        auto workers = DataService::instance()->get_remote_workers(full_name);
        if (!workers.empty()) {
            auto addr = DataService::instance()->get_worker_address(workers.front());
            std::lock_guard<std::mutex> lk(dep_loc_mutex_);
            task_dependency_locations_[merge_task_id][full_name] =
                CachedLocation{workers.front(), addr.host_, addr.port_};
        }
    }

    worker_manager_->assign_task(target_worker_id, merge_task_id);

    TaskAssignMessage assign;
    assign.task_id_ = merge_task_id;
    assign.task_name_ = "__merge_object";
    assign.task_module_ = "__fly_internal";
    // args: [short_name, source_db_path, target_db_path, target_data_path, source_host]
    assign.args_ = {short_name, source_db_path, target_db_path, target_data_path, source_host};
    // write_context_hash 从 provenance 取（保持对象来源可追溯）。
    {
        std::lock_guard<std::mutex> lk(provenance_mutex_);
        auto prov_it = write_provenance_.find(full_name);
        if (prov_it != write_provenance_.end()) {
            assign.write_context_hash_ = prov_it->second;
        }
    }

    {
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        auto it = worker_to_conn_.find(target_worker_id);
        if (it == worker_to_conn_.end()) {
            ERR("send_merge_task: target worker_id={} not connected", target_worker_id);
            std::lock_guard<std::mutex> mlk(merge_task_mutex_);
            merge_task_states_[merge_task_id].completed_ = true;
            merge_task_states_[merge_task_id].success_ = false;
            merge_task_states_[merge_task_id].error_message_ = "target worker not connected";
            merge_task_cv_.notify_all();
            return merge_task_id;
        }
        reactor_->send(it->second, assign);
    }

    INFO("Merge task assigned: task_id={}, target_worker={}, object={}, target_data_path={}",
         merge_task_id, target_worker_id, full_name, target_data_path);
    return merge_task_id;
}

void MasterAgent::on_merge_task_complete(uint64_t task_id, uint64_t worker_id, const CMVector<WrittenObject>& written_objects) {
    std::lock_guard<std::mutex> lk(merge_task_mutex_);
    auto it = merge_task_states_.find(task_id);
    if (it == merge_task_states_.end()) return;  // 非 merge task，忽略
    it->second.completed_ = true;
    it->second.success_ = true;
    it->second.worker_id_ = worker_id;
    for (const auto& wo : written_objects) {
        it->second.written_objects_.push_back(wo.object_name_);
    }
    merge_task_cv_.notify_all();
}

void MasterAgent::on_merge_task_failed(uint64_t task_id, const CMString& error_message) {
    std::lock_guard<std::mutex> lk(merge_task_mutex_);
    auto it = merge_task_states_.find(task_id);
    if (it == merge_task_states_.end()) return;
    it->second.completed_ = true;
    it->second.success_ = false;
    it->second.error_message_ = error_message;
    merge_task_cv_.notify_all();
}

bool MasterAgent::wait_merge_tasks_complete(const CMVector<uint64_t>& task_ids,
                                              int64_t timeout_seconds,
                                              CMVector<CMString>* completed_objects,
                                              CMVector<CMString>* failed_objects) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool all_ok = true;

    for (uint64_t tid : task_ids) {
        std::unique_lock<std::mutex> lk(merge_task_mutex_);
        if (!merge_task_cv_.wait_until(lk, deadline, [this, tid] {
            auto it = merge_task_states_.find(tid);
            return it != merge_task_states_.end() && it->second.completed_;
        })) {
            // 超时：本 task 未完成。
            all_ok = false;
            if (failed_objects) {
                failed_objects->push_back("TIMEOUT:merge_task_" + std::to_string(tid));
            }
            continue;
        }
        auto it = merge_task_states_.find(tid);
        if (it->second.success_) {
            if (completed_objects) {
                for (const auto& name : it->second.written_objects_) {
                    completed_objects->push_back(name);
                }
            }
        } else {
            all_ok = false;
            if (failed_objects) {
                failed_objects->push_back(it->second.error_message_.empty()
                    ? ("FAILED:merge_task_" + std::to_string(tid))
                    : it->second.error_message_);
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
        std::lock_guard<std::mutex> lk(delete_ack_mutex_);
        pending_delete_acks_[ack_key] = PendingDeleteData{};
    }

    DeleteDataMessage msg;
    msg.db_path_ = db_path;
    // data_path：显式传入优先；否则从 master db_instances_ 查（删源在 cleanup 前执行，
    // 此时 Database 仍是源的 data_path）。
    if (!data_path.empty()) {
        msg.data_path_ = data_path;
    } else {
        auto it = db_instances_.find(db_path);
        if (it != db_instances_.end()) {
            msg.data_path_ = it->second->get_data_path();
        }
    }
    msg.writer_ids_ = writer_ids;

    {
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        auto it = worker_to_conn_.find(source_worker_id);
        if (it == worker_to_conn_.end()) {
            ERR("send_delete_data: source worker_id={} not connected", source_worker_id);
            std::lock_guard<std::mutex> dlk(delete_ack_mutex_);
            auto& p = pending_delete_acks_[ack_key];
            p.completed_ = true;
            p.success_ = false;
            p.error_message_ = "source worker not connected";
            delete_ack_cv_.notify_all();
            return;
        }
        reactor_->send(it->second, msg);
    }
    INFO("DeleteData sent: worker_id={}, db_path={}, data_path={}, writer_ids_count={}",
         source_worker_id, db_path, data_path, writer_ids.size());
}

bool MasterAgent::wait_delete_data_acks(const CMVector<uint64_t>& source_worker_ids,
                                          const CMString& db_path,
                                          int64_t timeout_seconds,
                                          CMVector<uint64_t>* failed_workers) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool all_ok = true;

    for (uint64_t src_wid : source_worker_ids) {
        CMString ack_key = db_path + ":" + std::to_string(src_wid);
        std::unique_lock<std::mutex> lk(delete_ack_mutex_);
        if (!delete_ack_cv_.wait_until(lk, deadline, [this, &ack_key] {
            auto it = pending_delete_acks_.find(ack_key);
            return it != pending_delete_acks_.end() && it->second.completed_;
        })) {
            // 超时：本 worker 的 ack 未返回。
            all_ok = false;
            if (failed_workers) failed_workers->push_back(src_wid);
            WARN("wait_delete_data_acks: timeout for worker_id={}, db_path={}", src_wid, db_path);
            continue;
        }
        auto it = pending_delete_acks_.find(ack_key);
        if (!it->second.success_) {
            all_ok = false;
            if (failed_workers) failed_workers->push_back(src_wid);
            WARN("wait_delete_data_acks: worker_id={} delete failed: {}",
                 src_wid, it->second.error_message_);
        }
    }

    // 清理已处理的 ack 状态（防内存泄漏 —— 此前 on_delete_data_ack 只标 completed 不 erase）。
    {
        std::lock_guard<std::mutex> lk(delete_ack_mutex_);
        for (uint64_t src_wid : source_worker_ids) {
            CMString ack_key = db_path + ":" + std::to_string(src_wid);
            pending_delete_acks_.erase(ack_key);
        }
    }

    return all_ok;
}

void MasterAgent::on_delete_data_ack(uint64_t conn_id, const DeleteDataAckMessage& msg) {
    // worker_id_ 在 ack 里带回；用它和 db_path 组成 key 找到 pending 项。
    CMString ack_key = msg.db_path_ + ":" + std::to_string(msg.worker_id_);
    std::lock_guard<std::mutex> lk(delete_ack_mutex_);
    auto it = pending_delete_acks_.find(ack_key);
    if (it == pending_delete_acks_.end()) {
        DBG("DeleteDataAck for unknown key={}, ignoring", ack_key);
        return;
    }
    it->second.completed_ = true;
    it->second.success_ = msg.success_;
    it->second.deleted_count_ = msg.deleted_count_;
    it->second.error_message_ = msg.error_message_;
    delete_ack_cv_.notify_all();
    INFO("DeleteDataAck: worker_id={}, db_path={}, success={}, deleted={}",
         msg.worker_id_, msg.db_path_, msg.success_, msg.deleted_count_);
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
        std::lock_guard<std::mutex> mlk(merge_cleanup_mutex_);
        pending_merge_cleanups_[db_path] = PendingMergeCleanup{expected_acks, 0};
    }

    // 2. 广播 MergeCleanupMessage 给所有 worker：清旧 local_idx/remote_idx，
    //    按新路径 register_database + load 新 idx 重建 local_idx（同 host/共享 FS 可本地直读）。
    //    exempt = merge target workers（已持有效 local_idx，跳过清理但回 ack）。
    MergeCleanupMessage cleanup_msg;
    cleanup_msg.db_path_ = db_path;  // 源 db_path（worker 清旧索引用 + ack 匹配 pending key）
    cleanup_msg.data_path_ = merge_data_path;
    cleanup_msg.target_db_path_ = merge_db_path;  // 产物 db_path（idx 目录）
    cleanup_msg.exempt_worker_ids_ = merge_target_worker_ids;
    {
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, cleanup_msg);
        }
    }
    INFO("cleanup_after_merge: broadcast MergeCleanup for db_path={} to {} workers "
         "(exempt merge targets: {})", db_path, expected_acks, merge_target_worker_ids.size());

    // 3. 等待所有 worker 回 MergeCleanupAck（全局一致性屏障）。
    //    超时则告警但继续（尽力而为，不阻塞用户太久）。
    {
        std::unique_lock<std::mutex> mlk(merge_cleanup_mutex_);
        bool all_acked = merge_cleanup_cv_.wait_for(mlk, std::chrono::seconds(30),
            [this, &db_path] {
                auto it = pending_merge_cleanups_.find(db_path);
                return it != pending_merge_cleanups_.end() &&
                       it->second.received_count_ >= it->second.expected_count_;
            });
        if (!all_acked) {
            auto it = pending_merge_cleanups_.find(db_path);
            uint64_t got = (it != pending_merge_cleanups_.end()) ? it->second.received_count_ : 0;
            WARN("cleanup_after_merge: timeout waiting for MergeCleanupAck: "
                 "db_path={}, expected={}, received={}", db_path, expected_acks, got);
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
    {
        std::lock_guard<std::mutex> tlk(merge_task_mutex_);
        for (const auto& [tid, state] : merge_task_states_) {
            if (state.success_ && state.worker_id_ != 0) {
                for (const auto& obj : state.written_objects_) {
                    obj_to_workers[obj].push_back(state.worker_id_);
                }
            }
        }
    }
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
    auto db_it = db_instances_.find(db_path);
    if (db_it != db_instances_.end()) {
        db_it->second->set_paths(merge_db_path, merge_data_path);
    } else {
        // merge 产物句柄由 Python 经 ex_stg_create_database_with_id 构造，未进 master
        // db_instances_。这里用源 db_path 重建并登记，保证 master 路径权威源与新路径一致。
        auto db = CMMakeShared<Database>(merge_db_path, merge_data_path, 0, "", db_path);
        db_instances_[db_path] = db;
    }

    // 6. 清理本批 merge task 状态（wait_merge_tasks_complete 推迟到此处 erase）。
    {
        std::lock_guard<std::mutex> tlk(merge_task_mutex_);
        for (auto it = merge_task_states_.begin(); it != merge_task_states_.end(); ) {
            if (it->second.completed_) {
                it = merge_task_states_.erase(it);
            } else {
                ++it;
            }
        }
    }

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
    std::lock_guard<std::mutex> lk(merge_cleanup_mutex_);
    auto it = pending_merge_cleanups_.find(msg.db_path_);
    if (it == pending_merge_cleanups_.end()) {
        DBG("MergeCleanupAck for unknown db_path={}, ignoring", msg.db_path_);
        return;
    }
    it->second.received_count_++;
    DBG("MergeCleanupAck: db_path={}, worker_id={}, received={}/{}",
        msg.db_path_, msg.worker_id_, it->second.received_count_, it->second.expected_count_);
    if (it->second.received_count_ >= it->second.expected_count_) {
        merge_cleanup_cv_.notify_all();
    }
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
    uint64_t expected = 0;
    {
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        expected = worker_to_conn_.size();
    }

    if (expected == 0) {
        // 无 worker（单进程模式）：仅用 master 自身计数打印 summary。
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
    {
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        MessageCountRequestMessage req;
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, req);
        }
    }

    // 等所有 worker 上报（30s 超时容错，复刻 merge_cleanup_cv 模式）。
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

}  // namespace fly
