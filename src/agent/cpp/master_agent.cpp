#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), running_(false),
      graph_(std::make_unique<DependencyGraph>()),
      worker_manager_(std::make_unique<WorkerManager>()) {}

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
    
    reactor_ = std::make_unique<Reactor>(std::move(transport));
    
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
    
    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });
    
    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });
    
    scheduler_ = std::make_unique<TaskScheduler>(graph_.get(), worker_manager_.get());
    metadata_ = std::make_unique<MetadataManager>();
    heartbeat_monitor_ = std::make_unique<HeartbeatMonitor>(worker_manager_.get(), 30);
    
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
        heartbeat_check_running_ = false;
        if (heartbeat_check_thread_.joinable()) {
            heartbeat_check_thread_.join();
        }
        
        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
        
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
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
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
    
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "Worker registered: worker_id=" + std::to_string(worker_id));
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

void MasterAgent::on_task_complete(uint64_t conn_id, const TaskCompleteMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "Task complete: task_id=" + std::to_string(msg.task_id) +
                  ", written_objects=" + std::to_string(msg.written_objects.size()));
    }
    
    uint64_t worker_id = msg.worker_id;
    
    worker_manager_->complete_task(worker_id);
    
    for (const auto& data_path : msg.written_objects) {
        graph_->mark_data_ready(data_path);
        if (log) {
            log->debug("MasterAgent", "Mark data ready: " + data_path);
        }
    }
    
    graph_->remove_task(msg.task_id);
    
    metadata_->update_task_status(msg.task_id, TaskStatus::COMPLETED);
    
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

CMVector<uint64_t> MasterAgent::get_idle_workers() const {
    return worker_manager_->get_idle_workers();
}

}  // namespace fly