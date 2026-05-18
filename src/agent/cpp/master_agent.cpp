#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <core/cpp/config.h>
#include <thread>
#include <chrono>

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

    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "start() called, listening on " + host_ + ":" + std::to_string(port_));
    }

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
             auto* log = Logger::get_master();
             if (log) {
                 log->info("MasterAgent", "TaskSubmit received: task_name=" + msg.task_name +
                           ", module=" + msg.task_module);
             }
             uint64_t task_id = ++remote_task_counter_;
             submit_task(task_id, msg.task_name, msg.task_module, msg.args, msg.inputs, {});
         });

     reactor_->register_handler<DbPathRequestMessage>(
         [this](uint64_t conn_id, const DbPathRequestMessage& msg) {
             auto* log = Logger::get_master();
             if (log) {
                 log->info("MasterAgent", "DbPathRequest received: db_id=" + msg.db_id);
             }

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
             auto* log = Logger::get_master();
             if (log) {
                 log->info("MasterAgent", "DataQuery for object: " + msg.object_name);
             }

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

    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });

    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });

    scheduler_ = CMMakeUnique<TaskScheduler>(graph_.get(), worker_manager_.get());
    metadata_ = CMMakeUnique<MetadataManager>();

    heartbeat_monitor_ = CMMakeUnique<HeartbeatMonitor>(worker_manager_.get(), 30);

    heartbeat_check_running_ = true;
    heartbeat_check_thread_ = std::thread([this] { heartbeat_check_loop(); });

    reactor_thread_ = std::thread([this] { reactor_->run(); });
    running_ = true;

    if (log) {
        log->info("MasterAgent", "started, reactor thread running");
    }
}

void MasterAgent::stop() {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "stop() called");
    }

    if (running_) {
        ShutdownMessage shutdown_msg;
        for (const auto& [worker_id, conn_id] : worker_to_conn_) {
            if (log) {
                log->info("MasterAgent", "Broadcasting shutdown to worker_id=" + std::to_string(worker_id));
            }
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
                               const CMVector<CMString>& outputs) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "submit_task: id=" + std::to_string(task_id) + ", name=" + name);
    }

    metadata_->create_task(task_id, name, inputs, outputs, "{}");
    graph_->add_task(task_id, inputs);

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
}

void MasterAgent::assign_task_to_worker(uint64_t task_id, uint64_t worker_id) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "assign_task: task=" + std::to_string(task_id) + " to worker=" + std::to_string(worker_id));
    }

    auto conn_it = worker_to_conn_.find(worker_id);
    if (conn_it == worker_to_conn_.end()) {
        if (log) {
            log->error("MasterAgent", "worker not found: " + std::to_string(worker_id));
        }
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
                auto* log = Logger::get_master();
                if (log) {
                    log->warn("MasterAgent", "worker timeout: " + std::to_string(worker_id));
                }

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
    }

    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "Worker registered: worker_id=" + std::to_string(worker_id) +
                  ", data_server=" + msg.data_server_host + ":" + std::to_string(msg.data_server_port));
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

    auto* log = Logger::get_master();
    if (log) {
        log->debug("MasterAgent", "Heartbeat from worker_id=" + std::to_string(worker_id));
    }
}

void MasterAgent::on_data_ready(uint64_t conn_id, const DataReadyMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "DataReady: object=" + msg.object_name +
                  ", worker_id=" + std::to_string(msg.worker_id));
    }

    graph_->mark_data_ready(msg.object_name);

    auto addr = ds().get_worker_address(msg.worker_id);

    ds().update_remote_idx(msg.object_name, msg.worker_id, addr.host, addr.port);

    schedule_tasks();
}

void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "Task complete: task_id=" + std::to_string(msg.task_id) +
                  ", written_objects=" + std::to_string(msg.written_objects.size()));
    }

    uint64_t worker_id = msg.worker_id;

    worker_manager_->complete_task(worker_id);

    ds();
    auto addr = ds().get_worker_address(worker_id);

    bool streaming_mode = (Config::instance().get_int("dependency_update_mode") == 0);

    for (const auto& data_path : msg.written_objects) {
        if (!streaming_mode) {
            graph_->mark_data_ready(data_path);
            ds().update_remote_idx(data_path, worker_id, addr.host, addr.port);
            if (log) {
                log->debug("MasterAgent", "Recorded data location: " + data_path + " -> worker " + std::to_string(worker_id));
            }
        }
    }

    graph_->remove_task(msg.task_id);

    metadata_->update_task_status(msg.task_id, TaskStatus::COMPLETED);

    for (const auto& db_id : msg.frozen_dbs) {
        frozen_dbs_.insert(db_id);
        auto it = db_instances_.find(db_id);
        if (it != db_instances_.end()) {
            it->second->freeze();
        }
        if (log) {
            log->info("MasterAgent", "DB frozen: db_id=" + db_id);
        }
    }

    task_modules_.erase(msg.task_id);
    task_args_.erase(msg.task_id);

    schedule_tasks();
}

void MasterAgent::on_task_failed(uint64_t conn_id, const TaskFailedMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->error("MasterAgent", "Task failed: task_id=" + std::to_string(msg.task_id) + ", error=" + msg.error_message);
    }

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
        if (log) {
            log->error("MasterAgent", "FATAL: unrecoverable write error (type=" +
                std::to_string(static_cast<int>(msg.error_type)) + "): " + msg.error_message);
        }

        ShutdownMessage shutdown_msg;
        for (const auto& [wid, cid] : worker_to_conn_) {
            reactor_->send(cid, shutdown_msg);
        }
    }
}

void MasterAgent::on_disconnect(uint64_t conn_id) {
    auto* log = Logger::get_master();

    auto it = conn_to_worker_.find(conn_id);
    if (it != conn_to_worker_.end()) {
        uint64_t worker_id = it->second;
        conn_to_worker_.erase(conn_id);
        worker_to_conn_.erase(worker_id);
        worker_manager_->update_worker_status(worker_id, WorkerStatus::DEAD);

        if (log) {
            log->warn("MasterAgent", "Worker disconnected: worker_id=" + std::to_string(worker_id));
        }
    }
}

void MasterAgent::on_error(uint64_t conn_id, int error_code) {
    auto* log = Logger::get_master();
    if (log) {
        log->error("MasterAgent", "Connection error: conn_id=" + std::to_string(conn_id) + ", error=" + std::to_string(error_code));
    }
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
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "register_database: db_id=" + db_id + ", base_path=" + base_path + ", data_path=" + data_path);
    }

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
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "DataRequest for object: " + msg.object_name);
    }

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
            if (log) {
                log->debug("MasterAgent", "DataRequest read failed in db " + db_id +
                           " for " + msg.object_name + ": " + e.what());
            }
            continue;
        } catch (...) {
            if (log) {
                log->warn("MasterAgent", "DataRequest unknown error in db " + db_id +
                          " for " + msg.object_name);
            }
            continue;
        }
    }

    if (!response.success) {
        response.error_message = "Object not found on master: " + msg.object_name;
    }

    reactor_->send(conn_id, response);
}

void MasterAgent::on_write_register(uint64_t conn_id, const WriteRegisterMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "WriteRegister: worker=" + std::to_string(msg.worker_id) +
                  ", object=" + msg.object_name + ", db_id=" + msg.db_id);
    }

    WriteRegisterAckMessage ack;
    ack.object_name = msg.object_name;
    ack.db_id = msg.db_id;

    if (is_db_frozen(msg.db_id)) {
        ack.success = false;
        ack.error_message = "Database frozen: " + msg.db_id;
        ack.error_type = TaskErrorType::WRITE_TO_FROZEN_DB;
        if (log) {
            log->warn("MasterAgent", "WriteRegister rejected: db " + msg.db_id + " is frozen");
        }
    } else {
        ack.success = true;
    }

    reactor_->send(conn_id, ack);
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
    auto* log = Logger::get_master();

    if (log) {
        log->info("MasterAgent", "Direct DataClient request to " + host + ":" +
                  std::to_string(port) + " for " + object_name);
    }

    auto [success, data, error] = DataClient::request_data(host, port, object_name);

    if (!success) {
        throw std::runtime_error("Direct data request failed for " + object_name + ": " + error);
    }

    ReadResult result;
    result.data_buffer.assign(data.begin(), data.end());
    return result;
}

}  // namespace fly
