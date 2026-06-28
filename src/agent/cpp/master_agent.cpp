#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <core/cpp/config.h>
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
            submit_task(task_id, msg.task_name_, msg.task_module_, msg.args_, msg.inputs_, {}, msg.required_capabilities_, msg.attribute_timeout_, msg.write_context_hash_, msg.vars_);
        });

    reactor_->register_handler<DbPathRequestMessage>(
        [this](uint64_t conn_id, const DbPathRequestMessage& msg) {
            INFO("DbPathRequest received: db_id={}", msg.db_id_);

            DbPathResponseMessage response;
            response.db_id_ = msg.db_id_;

            auto it = db_registry_.find(msg.db_id_);
            if (it != db_registry_.end()) {
                response.base_path_ = it->second["base_path"];
                response.data_path_ = it->second["data_path"];
                response.success_ = true;
            } else {
                response.base_path_ = "";
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

    // Master reads its own data by looking up remote_idx directly — no network
    // DataQuery to self. A self-DataQuery would go through the reactor's epoll,
    // which doesn't guarantee ordering against worker WriteRegister on a
    // different fd. Direct lookup + DataClient::request_compressed_data avoids
    // the race entirely (the read sees remote_idx via mutex, same as
    // WriteRegister updates).
    dsInst->set_remote_compressed_read_handler([this](const CMString& name) -> std::tuple<bool, FlyBufferPtr, CMString, bool> {
        auto ds = DataService::instance();
        auto loc = ds->lookup_remote_idx(name);
        if (loc.worker_id_ == 0 || loc.host_.empty()) {
            bool has_pending = !graph_->get_pending_tasks().empty();
            bool has_running = metadata_->has_tasks_with_status(TaskStatus::RUNNING);
            return {false, nullptr, {}, has_pending || has_running};
        }
        auto [success, data, py_name, hash, error] =
            DataClient::request_compressed_data(loc.host_, loc.port_, name);
        if (success) {
            return {true, data, std::move(py_name), false};
        }
        return {false, nullptr, {}, false};
    });

    sigterm_received_ = false;

    INFO("MasterAgent started, reactor thread running");

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
    task_modules_.clear();
    task_args_.clear();

    running_ = false;
}

bool MasterAgent::is_running() const {
    return running_;
}

void MasterAgent::submit_task(uint64_t task_id, const CMString& name,
                               const CMString& module, const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& outputs,
                               const CMVector<CMString>& required_capabilities,
                               float attribute_timeout,
                               const CMString& write_context_hash,
                               const CMVector<CMString>& vars) {
    INFO("submit_task: id={}, name={}, attr_timeout={}, vars={}", task_id, name, attribute_timeout, vars.size());

    // module/args/vars must be set before graph_->add_task (concurrency: reactor thread's
    // schedule_tasks reads task_modules_ when task becomes ready)
    {
        std::lock_guard<std::mutex> lk(task_args_mutex_);
        task_modules_[task_id] = module;
        task_args_[task_id] = args;
        task_vars_[task_id] = vars;
    }

    // Var existence check (advisory only — does not affect scheduling).
    // vars are FULL names (db_id:short_name); split each to locate the Database.
    if (!vars.empty()) {
        for (const auto& full_var : vars) {
            CMString db_id, short_name;
            if (!split_full_name(full_var, db_id, short_name)) continue;
            auto db_it = db_instances_.find(db_id);
            if (db_it != db_instances_.end() && !db_it->second->master_has_var(short_name)) {
                WARN("task {} declares var '{}' but it does not exist on master (db={})",
                     task_id, short_name, db_id);
            }
        }
    }

    metadata_->create_task(task_id, name, inputs, outputs, "{}", required_capabilities, attribute_timeout);
    metadata_->set_write_context_hash(task_id, write_context_hash);

    TaskRequirements reqs;
    reqs.capabilities_ = required_capabilities;
    reqs.timeout_seconds_ = attribute_timeout;
    graph_->add_task(task_id, inputs, reqs);

    // Pre-fetch dependency locations at submit time (earliest possible point).
    {
        auto ds = DataService::instance();
        std::lock_guard<std::mutex> lock(dep_loc_mutex_);
        for (const auto& dep : inputs) {
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
             task_id, name, deps.size(), ready.size(), pending.size(), is_ready);
        for (const auto& dep : deps) {
            DBG("[DEP]   dep={} data_ready={}", dep, graph_->is_data_ready(dep));
        }
    }

    schedule_tasks();
}

void MasterAgent::schedule_tasks() {
    if (draining_.load()) return;

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

    // 每次调度前从 Config 同步开关，使运行时 set_int("locality_scheduling_enabled")
    // 即时生效（无需重启进程）。scheduler 启用后自行查询 DataService 算分。
    scheduler_->set_locality_preference(
        Config::instance()->get_int("locality_scheduling_enabled") == 1);

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

                    FailedTaskRecord record;
                    record.task_id_ = task_id;
                    auto task_opt2 = metadata_->get_task(task_id);
                    if (task_opt2) {
                        record.name_ = task_opt2->name_;
                        record.outputs_ = task_opt2->outputs_;
                        record.inputs_ = task_opt2->inputs_;
                        record.required_capabilities_ = task_opt2->required_capabilities_;
                    }
                    {
                        std::lock_guard<std::mutex> lk(task_args_mutex_);
                        record.module_ = task_modules_.count(task_id) ? task_modules_[task_id] : "";
                        record.args_ = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
                    }
                    record.error_message_ = error_msg;

                    graph_->remove_task(task_id);
                    metadata_->fail_task(task_id, error_msg);
                    {
                        std::lock_guard<std::mutex> lk(task_args_mutex_);
                        task_modules_.erase(task_id);
                        task_args_.erase(task_id);
                    }

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

            FailedTaskRecord record;
            record.task_id_ = task_id;
            auto task_opt = metadata_->get_task(task_id);
            if (task_opt) {
                record.name_ = task_opt->name_;
                record.inputs_ = task_opt->inputs_;
                record.outputs_ = task_opt->outputs_;
            }
            {
                std::lock_guard<std::mutex> lk(task_args_mutex_);
                record.module_ = task_modules_.count(task_id) ? task_modules_[task_id] : "";
                record.args_ = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
            }
            record.required_capabilities_ = requirements.capabilities_;
            record.error_message_ = error_msg;

            graph_->remove_task(task_id);
            metadata_->fail_task(task_id, error_msg);
            {
                std::lock_guard<std::mutex> lk(task_args_mutex_);
                task_modules_.erase(task_id);
                task_args_.erase(task_id);
            }

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
    auto task_opt3 = metadata_->get_task(task_id);
    msg.task_name_ = task_opt3 ? task_opt3->name_ : "";
    CMVector<CMString> declared_vars;
    {
        std::lock_guard<std::mutex> lk(task_args_mutex_);
        msg.task_module_ = task_modules_[task_id];
        msg.args_ = task_args_[task_id];
        auto vit = task_vars_.find(task_id);
        if (vit != task_vars_.end()) {
            declared_vars = vit->second;
        }
    }

    // Inline declared vars into the TaskAssignMessage so the worker receives
    // them in one shot (no extra round-trip). vars are FULL names; split each
    // to locate the Database and fetch the short-named value.
    if (!declared_vars.empty()) {
        for (const auto& full_var : declared_vars) {
            CMString db_id, short_name;
            if (!split_full_name(full_var, db_id, short_name)) continue;
            auto db_it = db_instances_.find(db_id);
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
        msg.write_context_hash_ = task_opt3->write_context_hash_;

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
        for (const auto& dep : task_opt3->inputs_) {
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

    worker_to_hostname_[worker_id] = msg.hostname_;
    worker_to_ip_[worker_id] = msg.ip_address_;

    worker_manager_->register_worker(worker_id, host_, port_, msg.attributes_);

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
void MasterAgent::record_worker_info(const CMString& object_name, const CMString& db_id,
                                      uint64_t worker_id, const CMString& writer_id_in) {
    CMString hostname;
    CMString ip;
    if (worker_id == 0) {
        hostname = ProcessInfo::instance()->hostname();
        ip = host_;
    } else {
        auto host_it = worker_to_hostname_.find(worker_id);
        if (host_it != worker_to_hostname_.end()) {
            hostname = host_it->second;
        }
        auto ip_it = worker_to_ip_.find(worker_id);
        if (ip_it != worker_to_ip_.end()) {
            ip = ip_it->second;
        }
    }

    if (hostname.empty()) return;

    CMString writer_id = writer_id_in;
    if (writer_id.empty()) {
        auto db_it2 = db_instances_.find(db_id);
        if (db_it2 != db_instances_.end()) {
            writer_id = db_it2->second->get_writer_id();
        }
    }

    auto key = std::make_tuple(db_id, hostname, writer_id);
    {
        std::lock_guard<std::mutex> lk(recorded_workers_mutex_);
        if (recorded_workers_.find(key) == recorded_workers_.end()) {
            recorded_workers_.insert(key);
            auto db_it = db_instances_.find(db_id);
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
void MasterAgent::evaluate_and_trigger_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_id) {
    auto target_replicas = static_cast<uint32_t>(Config::instance()->get_int("backup_replicas"));
    auto decision = DataService::instance()->evaluate_auto_backup(object_name, source_worker_id, target_replicas);
    if (decision.should_backup_) {
        trigger_auto_backup(object_name, source_worker_id, db_id);
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
                graph_->mark_data_ready(wo.object_name_);
                DataService::instance()->update_remote_idx(wo.object_name_, worker_id, addr.host_, addr.port_, wo.size_bytes_);
                update_dependency_location_cache(wo.object_name_, worker_id, addr.host_, addr.port_);
                record_worker_info(wo.object_name_, wo.db_id_, worker_id, "");
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

        {
            std::lock_guard<std::mutex> lk(task_args_mutex_);
            task_modules_.erase(msg.task_id_);
            task_args_.erase(msg.task_id_);
        }
    } else {
        // Internal tasks (backup, etc.) always update remote_idx
        for (const auto& wo : msg.written_objects_) {
            DataService::instance()->update_remote_idx(wo.object_name_, worker_id, addr.host_, addr.port_, wo.size_bytes_);
            DBG("Internal task: recorded data location: {} -> worker {}", wo.object_name_, worker_id);
        }
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

    // 在 erase task_modules_/task_args_ 前构建 FailedTaskRecord 并持久化，
    // 供 restart_failed_tasks 读取。运行时失败的 task（异常/读不到数据）
    // 也应可 restart，与调度时失败的 task 一致。
    {
        FailedTaskRecord record;
        record.task_id_ = msg.task_id_;
        record.error_message_ = msg.error_message_;
        auto task_opt = metadata_->get_task(msg.task_id_);
        if (task_opt) {
            record.name_ = task_opt->name_;
            record.outputs_ = task_opt->outputs_;
            record.inputs_ = task_opt->inputs_;
            record.required_capabilities_ = task_opt->required_capabilities_;
        }
        {
            std::lock_guard<std::mutex> lk(task_args_mutex_);
            record.module_ = task_modules_.count(msg.task_id_) ? task_modules_[msg.task_id_] : "";
            record.args_ = task_args_.count(msg.task_id_) ? task_args_[msg.task_id_] : CMVector<CMString>();
            task_modules_.erase(msg.task_id_);
            task_args_.erase(msg.task_id_);
        }
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

        CMString db_id, short_name;
        if (fly::split_full_name(obj, db_id, short_name)) {
            broadcast_object_removed(db_id, short_name);
        }
        WARN("Dirty object cleaned after task failure: task_id={}, object={}",
             msg.task_id_, obj);
    }

    if (msg.error_type_ == TaskErrorType::WRITE_REGISTRATION_TIMEOUT ||
        msg.error_type_ == TaskErrorType::EXECUTION_ERROR) {
        fatal_error_ = true;
        ERR("FATAL: unrecoverable error (type={}) for task_id={}: {}",
            static_cast<int>(msg.error_type_), msg.task_id_, msg.error_message_);
    }

    // 非 stream 模式：task 失败 → 按 task_id 回滚 pending frozen（防永久死锁）。
    // stream 模式下 pending 为空，此处 no-op。
    rollback_pending_frozen(msg.task_id_);

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

    auto tasks_to_recover = metadata_->get_task_ids_by_worker(worker_id);

    // 崩溃恢复：worker 断连可能意味着进程崩溃（收不到失败消息），必须按 task_id
    // 清掉这些 task 声明的 pending frozen，否则该 db 会被永久标"冻结中" → 后续所有
    // 写被拒 → 死锁级 bug。这是 Q1 选 task_id 而非 db_id 的核心理由。
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

            graph_->remove_task(task_id);
            TaskRequirements reqs;
            reqs.capabilities_ = task_opt4->required_capabilities_;
            reqs.timeout_seconds_ = task_opt4->attribute_timeout_;
            graph_->add_task(task_id, task_opt4->inputs_, reqs);
            metadata_->unassign_task(task_id);
            WARN("Recovered task from dead worker: task_id={}, name={}", task_id, task_opt4->name_);
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
    for (const auto& [worker_id, hostname] : worker_to_hostname_) {
        result.push_back({worker_id, hostname});
    }
    return result;
}

void MasterAgent::add_worker_hostname(uint64_t worker_id, const CMString& hostname) {
    worker_to_hostname_[worker_id] = hostname;
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

void MasterAgent::register_database(const CMString& db_id, const CMString& base_path, const CMString& data_path) {
    INFO("register_database: db_id={}, base_path={}, data_path={}", db_id, base_path, data_path);

    CMUnorderedMap<CMString, CMString> path_info;
    path_info["base_path"] = base_path;
    path_info["data_path"] = data_path;
    db_registry_[db_id] = path_info;
}

bool MasterAgent::is_db_frozen(const CMString& db_id) const {
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    return frozen_dbs_.count(db_id) > 0 || pending_frozen_dbs_.count(db_id) > 0;
}

bool MasterAgent::is_db_pending_frozen(const CMString& db_id) const {
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    return pending_frozen_dbs_.count(db_id) > 0;
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
    for (const auto& db_id : committed) {
        auto it = db_instances_.find(db_id);
        if (it != db_instances_.end()) it->second->freeze();
        INFO("DB frozen (committed by task): db_id={}, task_id={}", db_id, task_id);
        DatabaseFreezeNotification broadcast_msg;
        broadcast_msg.db_id_ = db_id;
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, broadcast_msg);
        }
    }
}

void MasterAgent::rollback_pending_frozen(uint64_t task_id) {
    // task 失败/崩溃：按 task_id 清除 pending（worker 本地 reset 由失败处理流程负责）。
    // 这是覆盖崩溃失败的关键：master 收不到失败消息，只能靠 task_id 反查清理。
    std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
    for (auto it = pending_frozen_dbs_.begin(); it != pending_frozen_dbs_.end(); ) {
        if (it->second == task_id) {
            WARN("Rolling back pending freeze: db_id={}, task_id={}", it->first, task_id);
            it = pending_frozen_dbs_.erase(it);
        } else {
            ++it;
        }
    }
}

CMSharedPtr<Database> MasterAgent::get_or_create_database(const CMString& base_path, const CMString& data_path, uint64_t writer_id) {
    auto db = CMMakeShared<Database>(base_path, data_path, writer_id);
    CMString db_id = db->get_db_id();
    db_instances_[db_id] = db;
    db_registry_[db_id]["base_path"] = base_path;
    db_registry_[db_id]["data_path"] = data_path;
    return db;
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
        auto loc = DataService::instance()->lookup_remote_idx(msg.object_name_);
        DBG("[TEMP-QUERY] DataQuery FOUND: obj={}, worker_id={}, host={}, port={}",
            msg.object_name_, loc.worker_id_, loc.host_, loc.port_);
        response.worker_id_ = loc.worker_id_;
        response.data_host_ = loc.host_;
        response.data_port_ = loc.port_;
        response.success_ = true;
        response.can_still_produce_ = false;

        if (Config::instance()->get_int("auto_backup_enabled") == 1) {
            DataService::instance()->record_remote_access(msg.object_name_);

            auto threshold = static_cast<uint64_t>(Config::instance()->get_int("backup_threshold"));
            auto target_replicas = static_cast<uint32_t>(Config::instance()->get_int("backup_replicas"));

            auto decision = DataService::instance()->evaluate_auto_backup(msg.object_name_, threshold, target_replicas);
            INFO("[AUTO-BACKUP] obj={}, read_count={}, current_replicas={}, target={}, should_backup={}",
                 msg.object_name_, decision.read_count_, decision.current_replicas_, target_replicas, decision.should_backup_);
            if (decision.should_backup_) {
                CMString db_id = msg.object_name_;
                auto colon_pos = msg.object_name_.find(':');
                if (colon_pos != CMString::npos) {
                    db_id = msg.object_name_.substr(0, colon_pos);
                }
                trigger_auto_backup(msg.object_name_, loc.worker_id_, db_id);
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
    DBG("WriteRegister: worker={}, object={}, db_id={}", msg.worker_id_, msg.object_name_, msg.db_id_);

    WriteRegisterAckMessage ack;
    ack.object_name_ = msg.object_name_;
    ack.db_id_ = msg.db_id_;

    bool registered_ok = false;
    if (is_db_frozen(msg.db_id_)) {
        ack.success_ = false;
        ack.error_message_ = "Database frozen: " + msg.db_id_;
        ack.error_type_ = TaskErrorType::WRITE_TO_FROZEN_DB;
        WARN("WriteRegister rejected: db {} is frozen", msg.db_id_);
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
            record_worker_info(msg.object_name_, msg.db_id_, msg.worker_id_, msg.writer_id_);
            if (master_self_write && Config::instance()->get_int("auto_backup_enabled") == 1) {
                evaluate_and_trigger_backup(msg.object_name_, 0, msg.db_id_);
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
    INFO("ObjectRemoved: object={}, db_id={}", msg.object_name_, msg.db_id_);

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

void MasterAgent::broadcast_object_removed(const CMString& db_id, const CMString& object_name) {
    CMString full = db_id + ":" + object_name;

    DataService::instance()->remove_remote_index(full);
    {
        std::lock_guard<std::mutex> lk(provenance_mutex_);
        write_provenance_.erase(full);
    }

    ObjectRemovedMessage msg;
    msg.object_name_ = full;
    msg.db_id_ = db_id;

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            reactor_->send(conn_id, msg);
        }
    }
}

// =============================================================================
// Var service handlers
// =============================================================================

void MasterAgent::on_var_set(uint64_t conn_id, const VarSetMessage& msg) {
    VarAckMessage ack;
    ack.var_name_ = msg.var_name_;  // echo the full name

    CMString db_id, short_name;
    auto it = (!split_full_name(msg.var_name_, db_id, short_name))
        ? db_instances_.end() : db_instances_.find(db_id);
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

    CMString db_id, short_name;
    auto it = (!split_full_name(msg.var_name_, db_id, short_name))
        ? db_instances_.end() : db_instances_.find(db_id);
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
    CMString db_id, short_name;
    if (split_full_name(msg.var_name_, db_id, short_name)) {
        auto it = db_instances_.find(db_id);
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
    INFO("RemoveRequest: object={}, db_id={}", msg.object_name_, msg.db_id_);

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
        cmd.db_id_ = msg.db_id_;
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
    ack.db_id_ = msg.db_id_;
    ack.object_name_ = msg.object_name_;
    ack.success_ = true;
    reactor_->send(conn_id, ack);

    schedule_tasks();

    INFO("RemoveRequest completed: object={}, workers_notified={}", msg.object_name_, worker_ids.size());
}

std::tuple<bool, FlyBufferPtr, CMString, bool> MasterAgent::request_remote_data(const CMString& object_name) {
    DataService::instance();

    auto info = DataService::instance()->lookup_remote_idx(object_name);
    if (info.host_.empty()) {
        bool has_pending = !graph_->get_pending_tasks().empty();
        bool has_running = metadata_->has_tasks_with_status(TaskStatus::RUNNING);
        return {false, nullptr, {}, has_pending || has_running};
    }

    auto [success, data, py_name, hash, error] = DataClient::request_compressed_data(info.host_, info.port_, object_name);

    if (!success) {
        ERR("request_remote_data compressed failed for {}: {}", object_name, error);
        return {false, nullptr, {}, false};
    }

    return {true, data, std::move(py_name), false};
}

std::pair<bool, ReadResult> MasterAgent::request_data_from_worker(const CMString& host, int32_t port,
                                                                   const CMString& object_name) {
    INFO("Direct DataClient request to {}:{} for {}", host, port, object_name);

    auto [success, compressed_data, py_name, hash, error] = DataClient::request_compressed_data(host, port, object_name);

    if (!success) {
        ERR("request_data_from_worker failed for {}: {}", object_name, error);
        return {false, ReadResult{}};
    }

    ReadResult result;
    result.data_buffer_.assign(compressed_data->data(), compressed_data->data() + compressed_data->size());
    result.py_name_ = std::move(py_name);
    return {true, std::move(result)};
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
        submit_task(record.task_id_, record.name_, record.module_, record.args_,
                    record.inputs_, record.outputs_, record.required_capabilities_);
    }

    INFO("Restarted {} failed tasks", record_count);
}

void MasterAgent::setup_write_context() {
    // master 自写对象的 record 阶段无需处理（register 已含全部 placement/schedule 逻辑）。
    // 留一个空 record_write_func 仅满足 is_active() 探测，不触发任何动作。
    WorkerAgentContext::set_record_write_func([](const CMString&, const CMString&, int64_t) {});
    WorkerAgentContext::set_register_func([this](const CMString& db_id, const CMString& name, int64_t compressed_size) -> std::pair<CMString, TaskErrorType> {
        return on_master_register_write(db_id, name, compressed_size);
    });
    WorkerAgentContext::set_freeze_func([this](const CMString& db_id) {
        on_master_freeze(db_id);
    });
    // Var funcs: master process operates directly on the authoritative Database
    // store (no network). The context passes FULL var names (db_id:short_name);
    // split off db_id to locate the Database, then query with the short name.
    WorkerAgentContext::set_set_var_func([this](const CMString& full_var_name,
                                                FlyBufferPtr value, const CMString& type_name) -> bool {
        CMString db_id, short_name;
        if (!split_full_name(full_var_name, db_id, short_name)) return false;
        auto it = db_instances_.find(db_id);
        if (it == db_instances_.end()) return false;
        return it->second->master_set_var(short_name, value, type_name);
    });
    WorkerAgentContext::set_get_var_func([this](const CMString& full_var_name)
        -> std::tuple<bool, FlyBufferPtr, CMString> {
        CMString db_id, short_name;
        if (!split_full_name(full_var_name, db_id, short_name)) return {false, nullptr, ""};
        auto it = db_instances_.find(db_id);
        if (it == db_instances_.end()) return {false, nullptr, ""};
        return it->second->master_get_var(short_name);
    });
    WorkerAgentContext::set_remove_var_func([this](const CMString& full_var_name) {
        CMString db_id, short_name;
        if (split_full_name(full_var_name, db_id, short_name)) {
            auto it = db_instances_.find(db_id);
            if (it != db_instances_.end()) {
                it->second->master_remove_var(short_name);
                broadcast_var(full_var_name, false);
            }
        }
    });
}

std::pair<CMString, TaskErrorType> MasterAgent::on_master_register_write(const CMString& db_id, const CMString& name, int64_t compressed_size) {
    if (!running_.load()) return {"", TaskErrorType::UNKNOWN};
    // master 自写走统一的 WriteRegisterMessage 路径（worker_id=0），与 worker 行为对称。
    // 同步调用 do_write_register，丢弃 ack（master 自写无需网络 ACK）。
    WriteRegisterMessage msg;
    msg.worker_id_ = 0;
    msg.object_name_ = db_id + ":" + name;
    msg.db_id_ = db_id;
    msg.size_bytes_ = compressed_size;
    auto db_it = db_instances_.find(db_id);
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

CMVector<IndexEntry> MasterAgent::restore_master_idx(const CMString& db_id,
                                                       const CMString& base_path,
                                                       const CMString& writer_id) {
    CMString idx_path = base_path + "/" + writer_id + ".idx";
    if (!std::filesystem::exists(idx_path)) {
        WARN("restore_master_idx: idx file not found: {}", idx_path);
        return {};
    }

    LocalIndex idx(idx_path);
    idx.load();
    auto entries = idx.get_all_entries();

    if (!entries.empty()) {
        DataService::instance()->restore_entries(db_id, entries);
        for (const auto& entry : entries) {
            graph_->mark_data_ready(entry.object_name_);
        }
        INFO("restore_master_idx: restored {} entries for db_id={}", entries.size(), db_id);
    }

    return entries;
}

void MasterAgent::send_idx_load_commands(const CMString& db_id,
                                           const CMString& base_path,
                                           const CMVector<CMString>& writer_ids) {
    IdxLoadCommandMessage msg;
    msg.db_id_ = db_id;
    msg.base_path_ = base_path;
    msg.writer_ids_ = writer_ids;

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            reactor_->send(conn_id, msg);
            INFO("Sent IdxLoadCommand to worker_id={}: db_id={}, writer_ids_count={}",
                 worker_id, db_id, writer_ids.size());
        }
    }
}

void MasterAgent::rebuild_remote_idx(const CMString& db_id,
                                       const CMString& base_path,
                                       const CMVector<::WorkerInfo>& workers) {
    CMUnorderedMap<CMString, CMString> old_id_to_hostname;
    for (const auto& w : workers) {
        old_id_to_hostname[std::to_string(w.worker_id_)] = w.hostname_;
    }

    CMUnorderedMap<CMString, CMVector<uint64_t>> hostname_to_new_workers;
    for (const auto& [worker_id, hostname] : worker_to_hostname_) {
        hostname_to_new_workers[hostname].push_back(worker_id);
    }

    for (const auto& w : workers) {
        if (w.writer_id_.empty()) {
            WARN("rebuild_remote_idx: empty writer_id for worker_id={}", w.worker_id_);
            continue;
        }

        CMString idx_path = base_path + "/" + w.writer_id_ + ".idx";
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
            DataService::instance()->update_remote_idx(entry.object_name_, new_worker_id, addr.host_, addr.port_);
            graph_->mark_data_ready(entry.object_name_);
        }
        INFO("rebuild_remote_idx: mapped {} entries from writer_id={} to new worker_id={}",
             entries.size(), w.writer_id_, new_worker_id);
    }
}

void MasterAgent::set_master_hostname(const CMString& hostname) {
    ProcessInfo::instance()->set_hostname(hostname);
}

void MasterAgent::send_idx_load_to_worker(const CMString& db_id,
                                            const CMString& base_path,
                                            const CMVector<CMString>& writer_ids,
                                            uint64_t worker_id) {
    IdxLoadCommandMessage msg;
    msg.db_id_ = db_id;
    msg.base_path_ = base_path;
    msg.writer_ids_ = writer_ids;

    std::lock_guard<std::mutex> lk(workers_mutex_);
    auto it = worker_to_conn_.find(worker_id);
    if (it == worker_to_conn_.end()) {
        ERR("send_idx_load_to_worker: worker_id={} not found", worker_id);
        return;
    }
    reactor_->send(it->second, msg);
    INFO("Sent IdxLoadCommand to worker_id={}: db_id={}, writer_ids_count={}",
         worker_id, db_id, writer_ids.size());
}

void MasterAgent::rebuild_remote_idx_for_worker(const CMString& db_id,
                                                   const CMString& base_path,
                                                   const CMVector<CMString>& writer_ids,
                                                   uint64_t worker_id) {
    auto addr = DataService::instance()->get_worker_address(worker_id);

    for (const auto& writer_id : writer_ids) {
        CMString idx_path = base_path + "/" + writer_id + ".idx";
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
            DataService::instance()->update_remote_idx(entry.object_name_, worker_id, addr.host_, addr.port_);
            graph_->mark_data_ready(entry.object_name_);
        }
        INFO("rebuild_remote_idx_for_worker: mapped {} entries from writer_id={} to worker_id={}",
             entries.size(), writer_id, worker_id);
    }
}

void MasterAgent::on_idx_load_ack(uint64_t conn_id, const IdxLoadAckMessage& msg) {
    INFO("IdxLoadAck: worker_id={}, db_id={}, success={}, loaded_count={}, writer_ids={}",
         msg.worker_id_, msg.db_id_, msg.success_, msg.loaded_count_, msg.loaded_writer_ids_.size());

    if (!msg.success_) {
        ERR("IdxLoadAck failed from worker_id={}: {}", msg.worker_id_, msg.error_message_);
        return;
    }

    // Master reads the same idx files from shared filesystem and updates remote_idx_
    auto it = db_registry_.find(msg.db_id_);
    if (it == db_registry_.end()) {
        ERR("IdxLoadAck: unknown db_id={}", msg.db_id_);
        return;
    }

    const CMString& base_path = it->second["base_path"];
    rebuild_remote_idx_for_worker(msg.db_id_, base_path, msg.loaded_writer_ids_, msg.worker_id_);
}

void MasterAgent::on_database_freeze_request(uint64_t conn_id, const DatabaseFreezeNotification& msg) {
    bool streaming_mode = (Config::instance()->get_int("dependency_update_mode") == 0);

    DatabaseFreezeAckMessage ack;
    ack.db_id_ = msg.db_id_;
    bool accepted = false;
    bool should_broadcast = false;   // stream 模式即时广播；非 stream 延迟到 commit

    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        bool already = frozen_dbs_.count(msg.db_id_) > 0 || pending_frozen_dbs_.count(msg.db_id_) > 0;
        if (already) {
            // 冲突：db 已被本 task 或其他 task freeze（业务流程错误）→ fail-fast
            ack.success_ = false;
            ack.error_type_ = TaskErrorType::DB_ALREADY_FROZEN;
            WARN("Freeze rejected (already frozen/pending): db_id={}, task_id={}",
                 msg.db_id_, msg.task_id_);
        } else if (streaming_mode) {
            // stream 模式：即时确认（保持原语义）
            frozen_dbs_.insert(msg.db_id_);
            ack.success_ = true;
            accepted = true;
            should_broadcast = true;
            INFO("DatabaseFreezeRequest (stream): db_id={}", msg.db_id_);
        } else {
            // 非 stream 模式：登记 pending（记 task_id），不广播、不本地 freeze
            pending_frozen_dbs_[msg.db_id_] = msg.task_id_;
            ack.success_ = true;
            accepted = true;
            INFO("DatabaseFreezeRequest (non-stream pending): db_id={}, task_id={}",
                 msg.db_id_, msg.task_id_);
        }
    }

    // 回 ack（两种模式都回，让 worker 同步确认 freeze 是否被接受）
    reactor_->send(conn_id, ack);

    if (accepted && streaming_mode) {
        // stream 模式的本地 freeze + 广播
        auto it = db_instances_.find(msg.db_id_);
        if (it != db_instances_.end()) {
            it->second->freeze();
        }
        DatabaseFreezeNotification broadcast_msg = msg;
        std::lock_guard<std::mutex> wlk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, broadcast_msg);
        }
        INFO("DB frozen and broadcasted (stream): db_id={}", msg.db_id_);
    }
}

void MasterAgent::on_master_freeze(const CMString& db_id) {
    if (!running_.load()) return;

    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        if (frozen_dbs_.count(db_id)) {
            WARN("DB already frozen, ignoring duplicate freeze: db_id={}", db_id);
            return;
        }

        frozen_dbs_.insert(db_id);
    }

    DatabaseFreezeNotification msg;
    msg.db_id_ = db_id;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, msg);
        }
    }
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
                task_modules_.clear();
                task_args_.clear();
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
        auto record = build_failed_record(task->task_id_);
        record.error_message_ = "Master shutdown: task still pending";
        persist_failed_task(record);
    }
}

FailedTaskRecord MasterAgent::build_failed_record(uint64_t task_id) {
    FailedTaskRecord record;
    record.task_id_ = task_id;
    auto task_opt6 = metadata_->get_task(task_id);
    if (task_opt6) {
        record.name_ = task_opt6->name_;
        {
            std::lock_guard<std::mutex> lk(task_args_mutex_);
            record.module_ = task_modules_.count(task_id) ? task_modules_[task_id] : "";
            record.args_ = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
        }
        record.inputs_ = task_opt6->inputs_;
        record.outputs_ = task_opt6->outputs_;
        record.required_capabilities_ = task_opt6->required_capabilities_;
        record.error_message_ = task_opt6->error_message_;
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
    CMString prefix = msg.db_id_ + ":";
    if (short_name.substr(0, prefix.size()) == prefix) {
        short_name = short_name.substr(prefix.size());
    }

    TaskAssignMessage assign;
    assign.task_id_ = backup_task_id;
    assign.task_name_ = "__backup_object";
    assign.task_module_ = "__fly_internal";
    assign.args_ = {short_name, msg.db_id_};

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

    CMString source_hostname;
    auto host_it = worker_to_hostname_.find(source_worker_id);
    if (host_it != worker_to_hostname_.end()) {
        source_hostname = host_it->second;
    }

    uint64_t fallback_worker = 0;
    for (const auto& [worker_id, hostname] : worker_to_hostname_) {
        if (worker_id == source_worker_id) continue;
        if (worker_id == 0) continue;

        auto worker_info_opt = worker_manager_->get_worker(worker_id);
        if (!worker_info_opt || worker_info_opt->get().status_ == WorkerStatus::DEAD) continue;

        if (hostname != source_hostname) {
            return worker_id;
        }
        if (fallback_worker == 0) {
            fallback_worker = worker_id;
        }
    }

    if (fallback_worker != 0) {
        INFO("select_backup_worker: all workers on same host, using worker_id={}", fallback_worker);
    }
    return fallback_worker;
}

void MasterAgent::trigger_auto_backup(const CMString& object_name, uint64_t source_worker_id, const CMString& db_id) {
    INFO("Auto-backup triggered: object={}, source_worker={}", object_name, source_worker_id);

    BackupRequestMessage backup_msg;
    backup_msg.worker_id_ = source_worker_id;
    backup_msg.object_name_ = object_name;
    backup_msg.db_id_ = db_id;

    on_backup_request(0, backup_msg);
}

}  // namespace fly