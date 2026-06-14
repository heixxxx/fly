#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <core/cpp/config.h>
#include <core/cpp/process_info.h>
#include <core/cpp/graceful_exit.h>
#include <storage/cpp/local_index.h>
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

    auto transport = create_transport("tcp");
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

    reactor_->register_handler<DataReadyMessage>(
        [this](uint64_t conn_id, const DataReadyMessage& msg) {
            on_data_ready(conn_id, msg);
        });

    reactor_->register_handler<TaskSubmitMessage>(
        [this](uint64_t conn_id, const TaskSubmitMessage& msg) {
            INFO("TaskSubmit received: task_name={}, module={}", msg.task_name_, msg.task_module_);
            uint64_t task_id = ++remote_task_counter_;
            submit_task(task_id, msg.task_name_, msg.task_module_, msg.args_, msg.inputs_, {}, msg.required_capabilities_, msg.write_context_hash_);
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

    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });

    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });

    scheduler_ = CMMakeUnique<TaskScheduler>(graph_.get(), worker_manager_.get());
    metadata_ = CMMakeUnique<TaskManager>();

    heartbeat_monitor_ = CMMakeUnique<HeartbeatMonitor>(
        worker_manager_.get(), Config::instance()->get_int("heartbeat_timeout"));

    heartbeat_check_running_ = true;
    heartbeat_check_thread_ = std::thread([this] { heartbeat_check_loop(); });

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

    dsInst->set_remote_compressed_read_handler([this](const CMString& name) -> std::tuple<bool, CMString, CMString, bool> {
        return request_remote_data(name);
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

    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            INFO("Broadcasting shutdown to worker_id={}", worker_id);
            reactor_->send(conn_id, ShutdownMessage{});
        }
    }

    {
        std::unique_lock<std::mutex> lock(drain_mutex_);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (true) {
            auto running_tasks = metadata_->get_tasks_by_status(TaskStatus::RUNNING);
            if (running_tasks.empty()) break;
            if (drain_cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                WARN("Shutdown drain timeout (10s), {} tasks still running",
                     running_tasks.size());
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
                               const CMString& write_context_hash) {
    INFO("submit_task: id={}, name={}", task_id, name);

    // module/args must be set before graph_->add_task (concurrency: reactor thread's
    // schedule_tasks reads task_modules_ when task becomes ready)
    {
        std::lock_guard<std::mutex> lk(task_args_mutex_);
        task_modules_[task_id] = module;
        task_args_[task_id] = args;
    }

    metadata_->create_task(task_id, name, inputs, outputs, "{}", required_capabilities);
    {
        auto task_opt = metadata_->get_task(task_id);
        if (task_opt) {
            task_opt->get().write_context_hash_ = write_context_hash;
        }
    }
    graph_->add_task(task_id, inputs, required_capabilities);
    
    {
        bool is_ready = graph_->is_task_ready(task_id);
        auto pending = graph_->get_pending_tasks();
        auto ready = graph_->get_ready_tasks();
        DBG("[DEP-GRAPH] submit_task: id={} name={} ready={} pending_count={} is_ready={} "
            "thread={}", task_id, name, ready.size(), pending.size(), is_ready,
            std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
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
    
    auto results = scheduler_->schedule_all_available();

    for (const auto& result : results) {
        if (result.scheduled_) {
            assign_task_to_worker(result.task_id_, result.worker_id_);
        }
    }

    if (Config::instance()->get_int("fail_unscheduleable_tasks") != 1) return;

    auto remaining = graph_->get_ready_tasks();
    for (uint64_t task_id : remaining) {
        auto requirements = graph_->get_task_requirements(task_id);
        if (requirements.empty()) continue;

        if (!worker_manager_->has_worker_with_all_capabilities(requirements)) {
            CMString cap_list;
            for (size_t i = 0; i < requirements.size(); i++) {
                if (i > 0) cap_list += ",";
                cap_list += requirements[i];
            }
            CMString error_msg = "No worker with required capabilities: [" + cap_list + "]";

            FailedTaskRecord record;
            record.task_id_ = task_id;
            auto task_opt = metadata_->get_task(task_id);
            if (task_opt) {
                TaskMetadata& task = task_opt->get();
                record.name_ = task.name_;
                record.inputs_ = task.inputs_;
                record.outputs_ = task.outputs_;
            }
            {
                std::lock_guard<std::mutex> lk(task_args_mutex_);
                record.module_ = task_modules_.count(task_id) ? task_modules_[task_id] : "";
                record.args_ = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
            }
            record.required_capabilities_ = requirements;
            record.error_message_ = error_msg;

            graph_->remove_task(task_id);
            metadata_->update_task_status(task_id, TaskStatus::FAILED);
            metadata_->set_error(task_id, error_msg);
            {
                std::lock_guard<std::mutex> lk(task_args_mutex_);
                task_modules_.erase(task_id);
                task_args_.erase(task_id);
            }

            persist_failed_task(record);
            ERR("Task {} failed: {}", task_id, error_msg);
        }
    }

    auto pending = graph_->get_pending_tasks();
    if (!pending.empty()) {
        auto ready = graph_->get_ready_tasks();
        auto running = metadata_->get_tasks_by_status(TaskStatus::RUNNING);
        if (ready.empty() && running.empty()) {
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
                    TaskMetadata& task2 = task_opt2->get();
                    record.name_ = task2.name_;
                    record.outputs_ = task2.outputs_;
                }
                record.module_ = task_modules_.count(task_id) ? task_modules_[task_id] : "";
                record.args_ = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
                record.inputs_ = deps;
                record.required_capabilities_ = graph_->get_task_requirements(task_id);
                record.error_message_ = error_msg;

                graph_->remove_task(task_id);
                metadata_->update_task_status(task_id, TaskStatus::FAILED);
                metadata_->set_error(task_id, error_msg);
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
    msg.task_name_ = task_opt3 ? task_opt3->get().name_ : "";
    {
        std::lock_guard<std::mutex> lk(task_args_mutex_);
        msg.task_module_ = task_modules_[task_id];
        msg.args_ = task_args_[task_id];
    }
    if (task_opt3) {
        msg.write_context_hash_ = task_opt3->get().write_context_hash_;
    }

    reactor_->send(conn_id, msg);

    metadata_->update_task_status(task_id, TaskStatus::RUNNING);
    metadata_->set_assigned_worker(task_id, worker_id);
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

void MasterAgent::on_data_ready(uint64_t conn_id, const DataReadyMessage& msg) {
    INFO("DataReady: object={}, worker_id={}", msg.object_name_, msg.worker_id_);

    graph_->mark_data_ready(msg.object_name_);

    auto addr = DataService::instance()->get_worker_address(msg.worker_id_);

    DataService::instance()->update_remote_idx(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);

    if (msg.worker_id_ == 0 && Config::instance()->get_int("auto_backup_enabled") == 1) {
        auto target_replicas = static_cast<uint32_t>(Config::instance()->get_int("backup_replicas"));
        auto decision = DataService::instance()->evaluate_auto_backup(msg.object_name_, 0, target_replicas);
        if (decision.should_backup_) {
            CMString db_id = msg.object_name_;
            auto colon_pos = msg.object_name_.find(':');
            if (colon_pos != CMString::npos) {
                db_id = msg.object_name_.substr(0, colon_pos);
            }
            trigger_auto_backup(msg.object_name_, 0, db_id);
        }
    }

    CMString hostname;
    CMString ip;
    if (msg.worker_id_ == 0) {
        hostname = ProcessInfo::instance()->hostname();
        ip = host_;
    } else {
        auto host_it = worker_to_hostname_.find(msg.worker_id_);
        if (host_it != worker_to_hostname_.end()) {
            hostname = host_it->second;
        }
        auto ip_it = worker_to_ip_.find(msg.worker_id_);
        if (ip_it != worker_to_ip_.end()) {
            ip = ip_it->second;
        }
    }

    if (!hostname.empty()) {
        CMString writer_id = msg.writer_id_;
        if (writer_id.empty()) {
            auto db_it2 = db_instances_.find(msg.db_id_);
            if (db_it2 != db_instances_.end()) {
                writer_id = db_it2->second->get_writer_id();
            }
        }

        auto key = std::make_tuple(msg.db_id_, hostname, writer_id);
        {
            std::lock_guard<std::mutex> lk(recorded_workers_mutex_);
            if (recorded_workers_.find(key) == recorded_workers_.end()) {
                recorded_workers_.insert(key);
                auto db_it = db_instances_.find(msg.db_id_);
                if (db_it != db_instances_.end()) {
                    ::WorkerInfo info;
                    info.worker_id_ = msg.worker_id_;
                    info.writer_id_ = writer_id;
                    info.hostname_ = hostname;
                    info.ip_address_ = ip;
                    info.launch_command_ = "";
                    db_it->second->append_worker_info_to_meta(info);
                }
            }
        }
    }

    schedule_tasks();
}

void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    size_t written_count = msg.written_objects_.size();
    INFO("Task complete: task_id={}, written_objects={}", msg.task_id_, written_count);

    uint64_t worker_id = msg.worker_id_;

    worker_manager_->complete_task(worker_id);

    DataService::instance();
    auto addr = DataService::instance()->get_worker_address(worker_id);

    bool streaming_mode = (Config::instance()->get_int("dependency_update_mode") == 0);

    for (const auto& data_path : msg.written_objects_) {
        if (!streaming_mode) {
            graph_->mark_data_ready(data_path);
            DataService::instance()->update_remote_idx(data_path, worker_id, addr.host_, addr.port_);
            DBG("Recorded data location: {} -> worker {}", data_path, worker_id);
        }
    }

    graph_->remove_task(msg.task_id_);
    metadata_->update_task_status(msg.task_id_, TaskStatus::COMPLETED);
    remove_persisted_task(msg.task_id_);
    INFO("Task cleanup complete: task_id={}, status=COMPLETED", msg.task_id_);

    for (const auto& db_id : msg.frozen_dbs_) {
        {
            std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
            frozen_dbs_.insert(db_id);
        }
        auto it = db_instances_.find(db_id);
        if (it != db_instances_.end()) {
            it->second->freeze();
        }
        INFO("DB frozen: db_id={}", db_id);

        DatabaseFreezeNotification freeze_msg;
        freeze_msg.db_id_ = db_id;
        {
            std::lock_guard<std::mutex> lk(workers_mutex_);
            for (const auto& [wid, cid] : worker_to_conn_) {
                reactor_->send(cid, freeze_msg);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(task_args_mutex_);
        task_modules_.erase(msg.task_id_);
        task_args_.erase(msg.task_id_);
    }

    schedule_tasks();
    notify_drain_if_active();
}

void MasterAgent::on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg) {
    ERR("Task failed: task_id={}, error={}", msg.task_id_, msg.error_message_);

    uint64_t worker_id = msg.worker_id_;

    worker_manager_->complete_task(worker_id);
    metadata_->update_task_status(msg.task_id_, TaskStatus::FAILED);
    metadata_->set_error(msg.task_id_, msg.error_message_);
    graph_->remove_task(msg.task_id_);

    {
        std::lock_guard<std::mutex> lk(task_args_mutex_);
        task_modules_.erase(msg.task_id_);
        task_args_.erase(msg.task_id_);
    }

    if (msg.error_type_ == TaskErrorType::WRITE_REGISTRATION_TIMEOUT ||
        msg.error_type_ == TaskErrorType::EXECUTION_ERROR) {
        fatal_error_ = true;
        ERR("FATAL: unrecoverable error (type={}) for task_id={}: {}",
            static_cast<int>(msg.error_type_), msg.task_id_, msg.error_message_);
    }

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

    if (!draining_.load()) {
        auto running_tasks = metadata_->get_tasks_by_status(TaskStatus::RUNNING);
        CMVector<uint64_t> tasks_to_recover;
        for (const auto& task : running_tasks) {
            if (task.assigned_worker_id_ == worker_id) {
                tasks_to_recover.push_back(task.task_id_);
            }
        }

        for (uint64_t task_id : tasks_to_recover) {
            auto task_opt4 = metadata_->get_task(task_id);
            if (!task_opt4) continue;
            TaskMetadata& meta = task_opt4->get();

            graph_->remove_task(task_id);
            graph_->add_task(task_id, meta.inputs_, meta.required_capabilities_);
            metadata_->update_task_status(task_id, TaskStatus::PENDING);
            metadata_->set_assigned_worker(task_id, 0);
            WARN("Recovered task from dead worker: task_id={}, name={}", task_id, meta.name_);
        }

        if (!tasks_to_recover.empty()) {
            schedule_tasks();
        }
    }
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
    auto tasks = metadata_->get_tasks_by_status(TaskStatus::RUNNING);
    CMVector<uint64_t> ids;
    for (const auto& t : tasks) {
        ids.push_back(t.task_id_);
    }
    return ids;
}

CMVector<uint64_t> MasterAgent::get_completed_tasks() const {
    auto tasks = metadata_->get_tasks_by_status(TaskStatus::COMPLETED);
    CMVector<uint64_t> ids;
    for (const auto& t : tasks) {
        ids.push_back(t.task_id_);
    }
    return ids;
}

CMVector<uint64_t> MasterAgent::get_failed_tasks() const {
    auto tasks = metadata_->get_tasks_by_status(TaskStatus::FAILED);
    CMVector<uint64_t> ids;
    for (const auto& t : tasks) {
        ids.push_back(t.task_id_);
    }
    return ids;
}

CMString MasterAgent::get_task_error(uint64_t task_id) const {
    auto task_opt5 = metadata_->get_task(task_id);
    if (task_opt5) {
        return task_opt5->get().error_message_;
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
    return frozen_dbs_.count(db_id) > 0;
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
        bool has_running = !metadata_->get_tasks_by_status(TaskStatus::RUNNING).empty();
        response.can_still_produce_ = has_pending || has_running;
        DBG("[TEMP-QUERY] DataQuery NOT FOUND: obj={}, can_still_produce={}", msg.object_name_, response.can_still_produce_);
    }

    reactor_->send(conn_id, response);
}

void MasterAgent::on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg) {
    INFO("WriteRegister: worker={}, object={}, db_id={}", msg.worker_id_, msg.object_name_, msg.db_id_);

    WriteRegisterAckMessage ack;
    ack.object_name_ = msg.object_name_;
    ack.db_id_ = msg.db_id_;

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
            ack.success_ = true;
            graph_->mark_data_ready(msg.object_name_);
            auto addr = DataService::instance()->get_worker_address(msg.worker_id_);
            DataService::instance()->update_remote_idx(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
            schedule_tasks();
        } else if (it->second == msg.write_context_hash_) {
            ack.success_ = true;
            graph_->mark_data_ready(msg.object_name_);
            auto addr = DataService::instance()->get_worker_address(msg.worker_id_);
            DataService::instance()->update_remote_idx(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
            schedule_tasks();
        } else {
            ack.success_ = false;
            ack.error_message_ = "Write provenance mismatch for " + msg.object_name_ +
                ": existing hash=" + it->second + " new hash=" + msg.write_context_hash_;
            ack.error_type_ = TaskErrorType::WRITE_PROVENANCE_MISMATCH;
            ERR("WriteRegister rejected: provenance mismatch for {}", msg.object_name_);
        }
    } else {
        ack.success_ = true;
        graph_->mark_data_ready(msg.object_name_);
        auto addr = DataService::instance()->get_worker_address(msg.worker_id_);
        DataService::instance()->update_remote_idx(msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
        DBG("[TEMP-REG-MASTER] WriteRegister success (no hash): obj={}, worker_id={}, host={}, port={}",
            msg.object_name_, msg.worker_id_, addr.host_, addr.port_);
        schedule_tasks();
        DBG("[DEP-GRAPH] after WriteRegister schedule: obj={} ready={} pending={} thread={}",
            msg.object_name_, graph_->get_ready_tasks().size(), 
            graph_->get_pending_tasks().size(),
            std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000);
    }

    reactor_->send(conn_id, ack);
}

void MasterAgent::on_worker_property_update(uint64_t conn_id, const WorkerPropertyUpdateMessage& msg) {
    size_t added_count = msg.added_properties_.size();
    size_t removed_count = msg.removed_properties_.size();
    INFO("WorkerPropertyUpdate: worker_id={}, added={}, removed={}", msg.worker_id_, added_count, removed_count);

    worker_manager_->update_capabilities(msg.worker_id_, msg.added_properties_, msg.removed_properties_);
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

std::tuple<bool, CMString, CMString, bool> MasterAgent::request_remote_data(const CMString& object_name) {
    DataService::instance();

    auto info = DataService::instance()->lookup_remote_idx(object_name);
    if (info.host_.empty()) {
        bool has_pending = !graph_->get_pending_tasks().empty();
        bool has_running = !metadata_->get_tasks_by_status(TaskStatus::RUNNING).empty();
        return {false, {}, {}, has_pending || has_running};
    }

    auto [success, data, py_name, hash, error] = DataClient::request_compressed_data(info.host_, info.port_, object_name);

    if (!success) {
        ERR("request_remote_data compressed failed for {}: {}", object_name, error);
        return {false, {}, {}, false};
    }

    return {true, std::move(data), std::move(py_name), false};
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
    result.data_buffer_.assign(compressed_data.begin(), compressed_data.end());
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
    WorkerAgentContext::set_record_write_func([this](const CMString& db_id, const CMString& name) {
        on_master_record_write(db_id, name);
    });
    WorkerAgentContext::set_register_func([this](const CMString& db_id, const CMString& name) -> std::pair<CMString, TaskErrorType> {
        return on_master_register_write(db_id, name);
    });
    WorkerAgentContext::set_freeze_func([this](const CMString& db_id) {
        on_master_freeze(db_id);
    });
}

void MasterAgent::on_master_record_write(const CMString& db_id, const CMString& name) {
    if (!running_.load()) return;
    DataReadyMessage msg;
    msg.worker_id_ = 0;
    msg.object_name_ = db_id + ":" + name;
    msg.db_id_ = db_id;
    auto db_it = db_instances_.find(db_id);
    if (db_it != db_instances_.end()) {
        msg.writer_id_ = db_it->second->get_writer_id();
    }
    on_data_ready(0, msg);
}

std::pair<CMString, TaskErrorType> MasterAgent::on_master_register_write(const CMString& db_id, const CMString& name) {
    if (!running_.load()) return {"", TaskErrorType::UNKNOWN};
    CMString full = db_id + ":" + name;
    graph_->mark_data_ready(full);

    auto addr = DataService::instance()->get_worker_address(0);
    DataService::instance()->update_remote_idx(full, 0, addr.host_, addr.port_);

    schedule_tasks();
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
    {
        std::lock_guard<std::mutex> lk(frozen_dbs_mutex_);
        if (frozen_dbs_.count(msg.db_id_)) {
            WARN("DB already frozen, ignoring duplicate freeze request: db_id={}", msg.db_id_);
            return;
        }

        INFO("DatabaseFreezeRequest: db_id={}", msg.db_id_);

        frozen_dbs_.insert(msg.db_id_);
    }

    auto it = db_instances_.find(msg.db_id_);
    if (it != db_instances_.end()) {
        it->second->freeze();
    }

    DatabaseFreezeNotification broadcast_msg = msg;
    {
        std::lock_guard<std::mutex> lk(workers_mutex_);
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, broadcast_msg);
        }
    }

    INFO("DB frozen and broadcasted: db_id={}", msg.db_id_);
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
        auto record = build_failed_record(task.task_id_);
        record.error_message_ = "Master shutdown: task still pending";
        persist_failed_task(record);
    }
}

FailedTaskRecord MasterAgent::build_failed_record(uint64_t task_id) {
    FailedTaskRecord record;
    record.task_id_ = task_id;
    auto task_opt6 = metadata_->get_task(task_id);
    if (task_opt6) {
        TaskMetadata& meta = task_opt6->get();
        record.name_ = meta.name_;
        {
            std::lock_guard<std::mutex> lk(task_args_mutex_);
            record.module_ = task_modules_.count(task_id) ? task_modules_[task_id] : "";
            record.args_ = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
        }
        record.inputs_ = meta.inputs_;
        record.outputs_ = meta.outputs_;
        record.required_capabilities_ = meta.required_capabilities_;
        record.error_message_ = meta.error_message_;
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