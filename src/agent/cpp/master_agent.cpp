#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <core/cpp/config.h>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fly {

std::atomic<uint64_t> MasterAgent::remote_task_counter_{100000};

DataService& MasterAgent::ds() {
    return data_service_ ? *data_service_ : DataService::instance();
}

void MasterAgent::set_data_service(DataService* ds) {
    data_service_ = ds;
    ds->set_remote_read_handler([this](const CMString& name) -> ReadResult {
        return request_remote_data(name);
    });
    ds->set_direct_read_handler([this](const CMString& host, int32_t port,
                                        const CMString& name) -> ReadResult {
        return request_data_from_worker(host, port, name);
    });
}

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), running_(false),
      graph_(CMMakeUnique<DependencyGraph>()),
      worker_manager_(CMMakeUnique<WorkerManager>()) {}

MasterAgent::~MasterAgent() {
    stop();
}

void MasterAgent::start() {
    if (running_) return;

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
            INFO("TaskSubmit received: task_name={}, module={}", msg.task_name, msg.task_module);
            uint64_t task_id = ++remote_task_counter_;
            submit_task(task_id, msg.task_name, msg.task_module, msg.args, msg.inputs, {}, msg.required_capabilities);
        });

    reactor_->register_handler<DbPathRequestMessage>(
        [this](uint64_t conn_id, const DbPathRequestMessage& msg) {
            INFO("DbPathRequest received: db_id={}", msg.db_id);

            DbPathResponseMessage response;
            response.db_id = msg.db_id;

            auto it = db_registry_.find(msg.db_id);
            if (it != db_registry_.end()) {
                response.base_path = it->second["base_path"];
                response.data_path = it->second["data_path"];
                response.success = true;
            } else {
                response.base_path = "";
                response.data_path = "";
                response.success = false;
            }

            reactor_->send(conn_id, response);
        });

    reactor_->register_handler<DataQueryMessage>(
        [this](uint64_t conn_id, const DataQueryMessage& msg) {
            INFO("DataQuery for object: {}", msg.object_name);

            ds();
            DataLocationMessage response;
            response.object_name = msg.object_name;

            if (ds().has_remote_location(msg.object_name)) {
                auto loc = ds().lookup_remote_idx(msg.object_name);
                response.worker_id = loc.worker_id;
                response.data_host = loc.host;
                response.data_port = loc.port;
                response.success = true;
            } else {
                response.success = false;
            }

            reactor_->send(conn_id, response);
        });

    reactor_->register_handler<DataRequestMessage>(
        [this](uint64_t conn_id, const DataRequestMessage& msg) {
            on_data_request(conn_id, msg);
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

    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });

    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });

    scheduler_ = CMMakeUnique<TaskScheduler>(graph_.get(), worker_manager_.get());
    metadata_ = CMMakeUnique<TaskManager>();

    heartbeat_monitor_ = CMMakeUnique<HeartbeatMonitor>(worker_manager_.get(), 30);

    heartbeat_check_running_ = true;
    heartbeat_check_thread_ = std::thread([this] { heartbeat_check_loop(); });

    reactor_thread_ = std::thread([this] { reactor_->run(); });
    running_ = true;

    INFO("MasterAgent started, reactor thread running");
}

void MasterAgent::stop() {
    INFO("MasterAgent stop() called");

    if (running_) {
        ShutdownMessage shutdown_msg;
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            INFO("Broadcasting shutdown to worker_id={}", worker_id);
            reactor_->send(conn_id, shutdown_msg);
        }

        heartbeat_check_running_ = false;
        heartbeat_check_cv_.notify_all();
        if (heartbeat_check_thread_.joinable()) {
            heartbeat_check_thread_.join();
        }

        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();

        db_instances_.clear();

        conn_to_worker_.clear();
        worker_to_conn_.clear();
        task_modules_.clear();
        task_args_.clear();

        running_ = false;
    }
}

bool MasterAgent::is_running() const {
    return running_;
}

void MasterAgent::submit_task(uint64_t task_id, const CMString& name,
                               const CMString& module, const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs,
                               const CMVector<CMString>& outputs,
                               const CMVector<CMString>& required_capabilities) {
    INFO("submit_task: id={}, name={}", task_id, name);

    metadata_->create_task(task_id, name, inputs, outputs, "{}", required_capabilities);
    graph_->add_task(task_id, inputs, required_capabilities);

    task_modules_[task_id] = module;
    task_args_[task_id] = args;

    schedule_tasks();
}

void MasterAgent::schedule_tasks() {
    auto results = scheduler_->schedule_all_available();

    for (const auto& result : results) {
        if (result.scheduled) {
            assign_task_to_worker(result.task_id, result.worker_id);
        }
    }

    if (Config::instance().get_int("fail_unscheduleable_tasks") != 1) return;

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
            record.task_id = task_id;
            record.name = metadata_->get_task(task_id) ? metadata_->get_task(task_id)->name : "";
            record.module = task_modules_.count(task_id) ? task_modules_[task_id] : "";
            record.args = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
            record.inputs = metadata_->get_task(task_id) ? metadata_->get_task(task_id)->inputs : CMVector<CMString>();
            record.outputs = metadata_->get_task(task_id) ? metadata_->get_task(task_id)->outputs : CMVector<CMString>();
            record.required_capabilities = requirements;
            record.error_message = error_msg;

            graph_->remove_task(task_id);
            metadata_->update_task_status(task_id, TaskStatus::FAILED);
            metadata_->set_error(task_id, error_msg);
            task_modules_.erase(task_id);
            task_args_.erase(task_id);

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
                record.task_id = task_id;
                record.name = metadata_->get_task(task_id) ? metadata_->get_task(task_id)->name : "";
                record.module = task_modules_.count(task_id) ? task_modules_[task_id] : "";
                record.args = task_args_.count(task_id) ? task_args_[task_id] : CMVector<CMString>();
                record.inputs = deps;
                record.outputs = metadata_->get_task(task_id) ? metadata_->get_task(task_id)->outputs : CMVector<CMString>();
                record.required_capabilities = graph_->get_task_requirements(task_id);
                record.error_message = error_msg;

                graph_->remove_task(task_id);
                metadata_->update_task_status(task_id, TaskStatus::FAILED);
                metadata_->set_error(task_id, error_msg);
                task_modules_.erase(task_id);
                task_args_.erase(task_id);

                persist_failed_task(record);
            ERR("Task {} failed: {}", task_id, error_msg);
            }
        }
    }
}

void MasterAgent::assign_task_to_worker(uint64_t task_id, uint64_t worker_id) {
    INFO("assign_task: task={} to worker={}", task_id, worker_id);

    auto conn_it = worker_to_conn_.find(worker_id);
    if (conn_it == worker_to_conn_.end()) {
        ERR("worker not found: {}", worker_id);
        return;
    }

    uint64_t conn_id = conn_it->second;

    TaskAssignMessage msg;
    msg.task_id = task_id;
    msg.task_name = metadata_->get_task(task_id)->name;
    msg.task_module = task_modules_[task_id];
    msg.args = task_args_[task_id];

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

        if (running_) {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

            heartbeat_monitor_->check_all_workers(timestamp);

            auto dead = heartbeat_monitor_->get_dead_workers();
            for (uint64_t worker_id : dead) {
                WARN("worker timeout: {}", worker_id);

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
    uint64_t worker_id = msg.worker_id;

    conn_to_worker_[conn_id] = worker_id;
    worker_to_conn_[worker_id] = conn_id;

    worker_manager_->register_worker(worker_id, host_, port_, msg.attributes);

    ds();
    if (msg.data_server_port > 0) {
        ds().register_worker(worker_id, msg.data_server_host, msg.data_server_port);
        INFO("Worker registered: worker_id={}, data_server={}:{}", worker_id, msg.data_server_host, msg.data_server_port);
    }

    RegisterAckMessage ack;
    ack.worker_id = worker_id;
    ack.master_address = host_;
    ack.master_port = static_cast<int32_t>(port_);
    reactor_->send(conn_id, ack);
}

void MasterAgent::on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg) {
    uint64_t worker_id = msg.worker_id;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    worker_manager_->set_heartbeat(worker_id, timestamp);

    DBG("Heartbeat from worker_id={}", worker_id);
}

void MasterAgent::on_data_ready(uint64_t conn_id, const DataReadyMessage& msg) {
    INFO("DataReady: object={}, worker_id={}", msg.object_name, msg.worker_id);

    graph_->mark_data_ready(msg.object_name);

    auto addr = ds().get_worker_address(msg.worker_id);

    ds().update_remote_idx(msg.object_name, msg.worker_id, addr.host, addr.port);

    schedule_tasks();
}

void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    size_t written_count = msg.written_objects.size();
    INFO("Task complete: task_id={}, written_objects={}", msg.task_id, written_count);

    uint64_t worker_id = msg.worker_id;

    worker_manager_->complete_task(worker_id);

    ds();
    auto addr = ds().get_worker_address(worker_id);

    bool streaming_mode = (Config::instance().get_int("dependency_update_mode") == 0);

    for (const auto& data_path : msg.written_objects) {
        if (!streaming_mode) {
            graph_->mark_data_ready(data_path);
            ds().update_remote_idx(data_path, worker_id, addr.host, addr.port);
            DBG("Recorded data location: {} -> worker {}", data_path, worker_id);
        }
    }

    graph_->remove_task(msg.task_id);

    metadata_->update_task_status(msg.task_id, TaskStatus::COMPLETED);

    remove_persisted_task(msg.task_id);

    for (const auto& db_id : msg.frozen_dbs) {
        frozen_dbs_.insert(db_id);
        auto it = db_instances_.find(db_id);
        if (it != db_instances_.end()) {
            it->second->freeze();
        }
        INFO("DB frozen: db_id={}", db_id);
    }

    task_modules_.erase(msg.task_id);
    task_args_.erase(msg.task_id);

    schedule_tasks();
}

void MasterAgent::on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg) {
    ERR("Task failed: task_id={}, error={}", msg.task_id, msg.error_message);

    uint64_t worker_id = msg.worker_id;

    worker_manager_->complete_task(worker_id);
    metadata_->update_task_status(msg.task_id, TaskStatus::FAILED);
    metadata_->set_error(msg.task_id, msg.error_message);

    task_modules_.erase(msg.task_id);
    task_args_.erase(msg.task_id);

    if (msg.error_type == TaskErrorType::WRITE_TO_FROZEN_DB ||
        msg.error_type == TaskErrorType::WRITE_REGISTRATION_FAILED ||
        msg.error_type == TaskErrorType::WRITE_REGISTRATION_TIMEOUT) {
        fatal_error_ = true;
        int error_type_val = static_cast<int>(msg.error_type);
        ERR("FATAL: unrecoverable write error (type={}): {}", error_type_val, msg.error_message);

        ShutdownMessage shutdown_msg;
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, shutdown_msg);
        }
    }
}

void MasterAgent::on_disconnect(uint64_t conn_id) {
    auto it = conn_to_worker_.find(conn_id);
    if (it != conn_to_worker_.end()) {
        uint64_t worker_id = it->second;
        conn_to_worker_.erase(conn_id);
        worker_to_conn_.erase(worker_id);
        worker_manager_->update_worker_status(worker_id, WorkerStatus::DEAD);

        WARN("Worker disconnected: worker_id={}", worker_id);
    }
}

void MasterAgent::on_error(uint64_t conn_id, int error_code) {
    ERR("Connection error: conn_id={}, error={}", conn_id, error_code);
    on_disconnect(conn_id);
}

CMVector<uint64_t> MasterAgent::get_connected_workers() const {
    CMVector<uint64_t> workers;
    for (const auto& [conn, worker] : conn_to_worker_) {
        workers.push_back(worker);
    }
    return workers;
}

size_t MasterAgent::get_connection_count() const {
    return conn_to_worker_.size();
}

CMVector<uint64_t> MasterAgent::get_pending_tasks() const {
    return graph_->get_pending_tasks();
}

CMVector<uint64_t> MasterAgent::get_running_tasks() const {
    auto tasks = metadata_->get_tasks_by_status(TaskStatus::RUNNING);
    CMVector<uint64_t> ids;
    for (const auto& t : tasks) {
        ids.push_back(t.task_id);
    }
    return ids;
}

CMVector<uint64_t> MasterAgent::get_completed_tasks() const {
    auto tasks = metadata_->get_tasks_by_status(TaskStatus::COMPLETED);
    CMVector<uint64_t> ids;
    for (const auto& t : tasks) {
        ids.push_back(t.task_id);
    }
    return ids;
}

CMVector<uint64_t> MasterAgent::get_failed_tasks() const {
    auto tasks = metadata_->get_tasks_by_status(TaskStatus::FAILED);
    CMVector<uint64_t> ids;
    for (const auto& t : tasks) {
        ids.push_back(t.task_id);
    }
    return ids;
}

CMString MasterAgent::get_task_error(uint64_t task_id) const {
    auto* meta = metadata_->get_task(task_id);
    if (meta) {
        return meta->error_message;
    }
    return "";
}

void MasterAgent::register_database(const CMString& db_id, const CMString& base_path, const CMString& data_path) {
    INFO("register_database: db_id={}, base_path={}, data_path={}", db_id, base_path, data_path);

    CMMap<CMString, CMString> path_info;
    path_info["base_path"] = base_path;
    path_info["data_path"] = data_path;
    db_registry_[db_id] = path_info;
}

bool MasterAgent::is_db_frozen(const CMString& db_id) const {
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

void MasterAgent::on_data_request(uint64_t conn_id, const DataRequestMessage& msg) {
    INFO("DataRequest for object: {}", msg.object_name);

    DataResponseMessage response;
    response.object_name = msg.object_name;
    response.success = false;

    for (const auto& [db_id, db] : db_instances_) {
        try {
            auto result = db->read_object_typed(msg.object_name);
            response.data.assign(result.data_buffer.begin(), result.data_buffer.end());
            response.success = true;
            break;
        } catch (const std::exception& e) {
            const char* err = e.what();
            DBG("DataRequest read failed in db {} for {}: {}", db_id, msg.object_name, err);
            continue;
        } catch (...) {
            WARN("DataRequest unknown error in db {} for {}", db_id, msg.object_name);
            continue;
        }
    }

    if (!response.success) {
        response.error_message = "Object not found on master: " + msg.object_name;
    }

    reactor_->send(conn_id, response);
}

void MasterAgent::on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg) {
    INFO("WriteRegister: worker={}, object={}, db_id={}", msg.worker_id, msg.object_name, msg.db_id);

    WriteRegisterAckMessage ack;
    ack.object_name = msg.object_name;
    ack.db_id = msg.db_id;

    if (is_db_frozen(msg.db_id)) {
        ack.success = false;
        ack.error_message = "Database frozen: " + msg.db_id;
        ack.error_type = TaskErrorType::WRITE_TO_FROZEN_DB;
        WARN("WriteRegister rejected: db {} is frozen", msg.db_id);
    } else {
        ack.success = true;
    }

    reactor_->send(conn_id, ack);
}

void MasterAgent::on_worker_property_update(uint64_t conn_id, const WorkerPropertyUpdateMessage& msg) {
    size_t added_count = msg.added_properties.size();
    size_t removed_count = msg.removed_properties.size();
    INFO("WorkerPropertyUpdate: worker_id={}, added={}, removed={}", msg.worker_id, added_count, removed_count);

    worker_manager_->update_capabilities(msg.worker_id, msg.added_properties, msg.removed_properties);
}

void MasterAgent::on_object_removed(uint64_t conn_id, const ObjectRemovedMessage& msg) {
    INFO("ObjectRemoved: object={}, db_id={}", msg.object_name, msg.db_id);

    ds().remove_remote_index(msg.object_name);

    ObjectRemovedMessage broadcast_msg = msg;
    for (const auto& [worker_id, worker_conn_id] : worker_to_conn_) {
        if (worker_conn_id != conn_id) {
            reactor_->send(worker_conn_id, broadcast_msg);
        }
    }
}

void MasterAgent::broadcast_object_removed(const CMString& db_id, const CMString& object_name) {
    CMString full = db_id + ":" + object_name;

    ds().remove_remote_index(full);

    ObjectRemovedMessage msg;
    msg.object_name = full;
    msg.db_id = db_id;

    for (const auto& [worker_id, conn_id] : worker_to_conn_) {
        reactor_->send(conn_id, msg);
    }
}

ReadResult MasterAgent::request_remote_data(const CMString& object_name) {
    ds();

    auto info = ds().lookup_remote_idx(object_name);
    if (info.host.empty()) {
        throw std::runtime_error("No remote location found for: " + object_name);
    }

    auto [success, data, error] = DataClient::request_data(info.host, info.port, object_name);

    if (!success) {
        throw std::runtime_error("Data transfer failed for " + object_name + ": " + error);
    }

    ReadResult result;
    result.data_buffer.assign(data.begin(), data.end());
    return result;
}

ReadResult MasterAgent::request_data_from_worker(const CMString& host, int32_t port,
                                                   const CMString& object_name) {
    INFO("Direct DataClient request to {}:{} for {}", host, port, object_name);

    auto [success, data, error] = DataClient::request_data(host, port, object_name);

    if (!success) {
        throw std::runtime_error("Direct data request failed for " + object_name + ": " + error);
    }

    ReadResult result;
    result.data_buffer.assign(data.begin(), data.end());
    return result;
}

CMString MasterAgent::get_failed_tasks_file_path() const {
    CMString log_dir = Config::instance().get_str("log_dir");
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
        throw std::runtime_error("Failed to open failed tasks file: " + file_path);
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

    ERR("Task {} failed and persisted. To restart after fixing, call restart_failed_tasks(\"{}\")", record.task_id, file_path);
}

void MasterAgent::remove_persisted_task(uint64_t task_id) {
    CMString file_path = get_failed_tasks_file_path();
    if (!std::filesystem::exists(file_path)) return;

    auto records = read_failed_records(file_path);
    if (records.empty()) return;

    size_t before = records.size();
    records.erase(
        std::remove_if(records.begin(), records.end(),
            [task_id](const FailedTaskRecord& r) { return r.task_id == task_id; }),
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
        metadata_->remove_task(record.task_id);

        for (const auto& input : record.inputs) {
            if (!graph_->is_data_ready(input)) {
                auto [found, _result] = ds().try_read_local(input);
                if (found || !ds().lookup_remote_idx(input).host.empty()) {
                    graph_->mark_data_ready(input);
                }
            }
        }

        submit_task(record.task_id, record.name, record.module, record.args,
                    record.inputs, record.outputs, record.required_capabilities);
    }

    INFO("Restarted {} failed tasks", record_count);
}

}  // namespace fly