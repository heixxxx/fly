#include <agent/cpp/worker_agent.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

WorkerAgent::WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port)
    : worker_id_(worker_id), master_host_(master_host), master_port_(master_port),
      running_(false), registered_(false) {}

WorkerAgent::~WorkerAgent() {
    stop();
}

void WorkerAgent::start() {
    if (running_) return;
    
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "start() called, connecting to " + master_host_ + ":" + std::to_string(master_port_));
    }
    
    auto transport = create_transport("tcp");
    
    transport->listen("0.0.0.0", 0);
    data_server_port_ = static_cast<int32_t>(transport->get_bound_port());
    
    if (log) {
        log->info("WorkerAgent", "data server listening on port " + std::to_string(data_server_port_));
    }
    
    master_conn_ = transport->connect(master_host_, master_port_);
    
    if (log) {
        log->info("WorkerAgent", "connected, master_conn=" + std::to_string(master_conn_));
    }
    
    reactor_ = std::make_unique<Reactor>(std::move(transport));
    
     reactor_->register_handler<RegisterAckMessage>(
         [this](uint64_t conn, const RegisterAckMessage& msg) {
             on_register_ack(msg);
         });
     
     reactor_->register_handler<TaskAssignMessage>(
         [this](uint64_t conn, const TaskAssignMessage& msg) {
             on_task_assign(msg);
         });
     
     reactor_->register_handler<ShutdownMessage>(
         [this](uint64_t conn, const ShutdownMessage& msg) {
             on_shutdown(msg);
         });
     
     reactor_->register_handler<DbPathResponseMessage>(
         [this](uint64_t conn, const DbPathResponseMessage& msg) {
             on_db_path_response(msg);
         });
     
     reactor_->register_handler<DataRequestMessage>(
         [this](uint64_t conn_id, const DataRequestMessage& msg) {
             on_data_request(conn_id, msg);
         });
     
     reactor_->register_handler<DataLocationMessage>(
         [this](uint64_t conn_id, const DataLocationMessage& msg) {
             on_data_location(conn_id, msg);
         });
     
     reactor_->register_handler<DataResponseMessage>(
         [this](uint64_t conn_id, const DataResponseMessage& msg) {
             on_data_response(conn_id, msg);
         });
    
    reactor_thread_ = std::thread([this] { reactor_->run(); });
    
    RegisterMessage reg;
    reg.worker_id = worker_id_;
    reg.data_server_host = "127.0.0.1";
    reg.data_server_port = data_server_port_;
    reactor_->send(master_conn_, reg);
    
    if (log) {
        log->info("WorkerAgent", "RegisterMessage sent with data_server_port=" + std::to_string(data_server_port_));
    }
    
    heartbeat_running_ = true;
    heartbeat_thread_ = std::thread([this] { heartbeat_loop(); });
    
    running_ = true;
}

void WorkerAgent::stop() {
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "stop() called");
    }
    
    if (running_) {
        heartbeat_running_ = false;
        heartbeat_cv_.notify_all();
        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }
        
        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
        
        running_ = false;
        registered_ = false;
    }
}

bool WorkerAgent::is_running() const {
    return running_;
}

uint64_t WorkerAgent::get_worker_id() const {
    return worker_id_;
}

void WorkerAgent::set_executor(std::shared_ptr<TaskExecutor> executor) {
    executor_ = std::move(executor);
}

bool WorkerAgent::is_registered() const {
    return registered_;
}

void WorkerAgent::submit_task(const CMString& name, const CMString& module,
                               const CMVector<CMString>& args,
                               const CMVector<CMString>& inputs) {
    TaskSubmitMessage msg;
    msg.task_name = name;
    msg.task_module = module;
    msg.args = args;
    msg.inputs = inputs;
    reactor_->send(master_conn_, msg);
}

void WorkerAgent::heartbeat_loop() {
    auto* log = Logger::get_worker(worker_id_);
    while (heartbeat_running_) {
        {
            std::unique_lock<std::mutex> lock(heartbeat_mutex_);
            heartbeat_cv_.wait_for(lock, std::chrono::seconds(10),
                                    [this]{ return !heartbeat_running_.load(); });
        }
        
        if (registered_ && heartbeat_running_) {
            HeartbeatMessage hb;
            hb.worker_id = worker_id_;
            reactor_->send(master_conn_, hb);
            
            if (log) {
                log->debug("WorkerAgent", "Heartbeat sent");
            }
        }
    }
}

void WorkerAgent::on_register_ack(const RegisterAckMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    registered_ = true;
    
    if (log) {
        log->info("WorkerAgent", "RegisterAck received, registered");
    }
}

void WorkerAgent::on_task_assign(const TaskAssignMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "TaskAssign received: task_id=" + std::to_string(msg.task_id));
    }
    
    PendingTask task;
    task.task_id = msg.task_id;
    task.task_name = msg.task_name;
    task.task_module = msg.task_module;
    task.args = msg.args;
    
    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        task_queue_.push(std::move(task));
    }
}

bool WorkerAgent::has_pending_task() const {
    std::lock_guard<std::mutex> lock(task_queue_mutex_);
    return !task_queue_.empty();
}

bool WorkerAgent::poll_task() {
    PendingTask task;
    {
        std::lock_guard<std::mutex> lock(task_queue_mutex_);
        if (task_queue_.empty()) return false;
        task = std::move(task_queue_.front());
        task_queue_.pop();
    }
    
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "Executing task: task_id=" + std::to_string(task.task_id));
    }
    
    if (executor_) {
        begin_task(task.task_id);
        auto result = executor_->execute(
            task.task_id, task.task_name, task.task_module, task.args);
        auto tracked_writes = end_task(task.task_id);
        
        if (result.status == TaskExecStatus::SUCCESS) {
            TaskCompleteMessage complete;
            complete.task_id = task.task_id;
            complete.worker_id = worker_id_;
            complete.written_objects = std::move(tracked_writes);
            for (auto& out : result.outputs) {
                complete.written_objects.push_back(std::move(out));
            }
            complete.frozen_dbs = std::move(result.frozen_dbs);
            reactor_->send(master_conn_, complete);
            
            if (log) {
                log->info("WorkerAgent", "TaskComplete sent: task_id=" + std::to_string(task.task_id) +
                          ", outputs=" + std::to_string(complete.written_objects.size()));
            }
        } else {
            TaskFailedMessage failed;
            failed.task_id = task.task_id;
            failed.worker_id = worker_id_;
            failed.error_message = result.error;
            reactor_->send(master_conn_, failed);
            
            if (log) {
                log->error("WorkerAgent", "TaskFailed sent: task_id=" + std::to_string(task.task_id) + ", error=" + result.error);
            }
        }
    }
    return true;
}

void WorkerAgent::on_shutdown(const ShutdownMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    registered_ = false;
    
    if (log) {
        log->info("WorkerAgent", "Shutdown received");
    }
}

void WorkerAgent::on_db_path_response(const DbPathResponseMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "DbPathResponse received: db_id=" + msg.db_id + 
                  ", base_path=" + msg.base_path + ", data_path=" + msg.data_path);
    }
}

void WorkerAgent::begin_task(uint64_t task_id) {
    current_task_id_ = task_id;
    current_writes_.clear();
    WorkerAgentContext::set(&record_write_trampoline, this);
}

void WorkerAgent::record_write(const CMString& db_id, const CMString& object_name) {
    CMString full_name = db_id + ":" + object_name;
    current_writes_.push_back(full_name);
}

CMVector<CMString> WorkerAgent::end_task(uint64_t task_id) {
    WorkerAgentContext::clear();
    auto writes = std::move(current_writes_);
    current_writes_.clear();
    current_task_id_ = 0;
    return writes;
}

void WorkerAgent::record_write_trampoline(void* ctx, const CMString& db_id, const CMString& name) {
    static_cast<WorkerAgent*>(ctx)->record_write(db_id, name);
}

void WorkerAgent::on_data_request(uint64_t conn_id, const DataRequestMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "DataRequest for object: " + msg.object_name);
    }
    
    DataResponseMessage response;
    response.object_name = msg.object_name;
    response.success = false;
    
    for (const auto& [db_id, db] : databases_) {
        try {
            auto result = db->read_object_typed(msg.object_name);
            response.data.assign(result.data_buffer.begin(), result.data_buffer.end());
            response.success = true;
            break;
        } catch (...) {
            continue;
        }
    }
    
    if (!response.success) {
        response.error_message = "Object not found: " + msg.object_name;
    }
    
    reactor_->send(conn_id, response);
}

void WorkerAgent::on_data_response(uint64_t conn_id, const DataResponseMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "DataResponse for object: " + msg.object_name +
                  ", success=" + std::to_string(msg.success));
    }
    
    std::lock_guard<std::mutex> lock(pending_data_mutex_);
    auto it = pending_data_.find(msg.object_name);
    if (it != pending_data_.end()) {
        it->second->data = msg.data;
        it->second->success = msg.success;
        it->second->error_message = msg.error_message;
        it->second->completed = true;
    }
}

void WorkerAgent::on_data_location(uint64_t conn_id, const DataLocationMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    if (log) {
        log->info("WorkerAgent", "DataLocation for object: " + msg.object_name +
                  ", worker_id=" + std::to_string(msg.worker_id) +
                  ", data_host=" + msg.data_host + ":" + std::to_string(msg.data_port) +
                  ", success=" + std::to_string(msg.success));
    }
    
    std::lock_guard<std::mutex> lock(pending_data_mutex_);
    auto it = pending_data_.find(msg.object_name);
    if (it == pending_data_.end() || !msg.success) {
        if (it != pending_data_.end()) {
            it->second->completed = true;
            it->second->success = false;
            it->second->error_message = "DataLocation not found";
        }
        return;
    }
    
    it->second->data_host = msg.data_host;
    it->second->data_port = msg.data_port;
    it->second->target_worker_id = msg.worker_id;
    it->second->location_received = true;
}

ReadResult WorkerAgent::request_remote_data(const CMString& object_name) {
    auto* log = Logger::get_worker(worker_id_);
    
    auto pending = std::make_shared<PendingRemoteData>();
    pending->object_name = object_name;
    {
        std::lock_guard<std::mutex> lock(pending_data_mutex_);
        pending_data_[object_name] = pending;
    }
    
    DataQueryMessage query;
    query.object_name = object_name;
    reactor_->send(master_conn_, query);
    
    if (log) {
        log->info("WorkerAgent", "Sent DataQuery for " + object_name);
    }
    
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> lock(pending_data_mutex_);
        if (pending->location_received && !pending->completed) {
            pending->location_received = false;
            CMString host = pending->data_host;
            int32_t port = pending->data_port;
            
            uint64_t target_conn = reactor_->connect(host, port);
            
            DataRequestMessage req;
            req.object_name = object_name;
            req.requesting_worker_id = worker_id_;
            reactor_->send(target_conn, req);
            
            if (log) {
                log->info("WorkerAgent", "Connected to " + host + ":" + std::to_string(port) +
                          ", sent DataRequest for " + object_name);
            }
            continue;
        }
        if (pending->completed) {
            pending_data_.erase(object_name);
            if (pending->success) {
                ReadResult result;
                result.data_buffer.assign(pending->data.begin(), pending->data.end());
                result.py_name = "";
                return result;
            } else {
                throw std::runtime_error("Remote data request failed for " + object_name + ": " + pending->error_message);
            }
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(pending_data_mutex_);
        pending_data_.erase(object_name);
    }
    throw std::runtime_error("Remote data request timeout for: " + object_name);
}

void WorkerAgent::register_database(const CMString& db_id, std::shared_ptr<Database> db) {
    databases_[db_id] = std::move(db);
}

std::shared_ptr<Database> WorkerAgent::get_database(const CMString& db_id) const {
    auto it = databases_.find(db_id);
    if (it != databases_.end()) {
        return it->second;
    }
    return nullptr;
}

}  // namespace fly