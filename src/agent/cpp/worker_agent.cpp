#include <agent/cpp/worker_agent.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

WorkerAgent::WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port)
    : worker_id_(worker_id), master_host_(master_host), master_port_(master_port),
      running_(false), registered_(false), executor_(nullptr) {}

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
    
    reactor_thread_ = std::thread([this] { reactor_->run(); });
    
    RegisterMessage reg;
    reg.worker_id = worker_id_;
    reactor_->send(master_conn_, reg);
    
    if (log) {
        log->info("WorkerAgent", "RegisterMessage sent");
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

void WorkerAgent::set_executor(TaskExecutor* executor) {
    executor_ = executor;
}

bool WorkerAgent::is_registered() const {
    return registered_;
}

void WorkerAgent::heartbeat_loop() {
    auto* log = Logger::get_worker(worker_id_);
    while (heartbeat_running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
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
    
    if (executor_) {
        auto result = executor_->execute(
            msg.task_id, msg.task_name, msg.task_module, msg.args);
        
        if (result.status == TaskExecStatus::SUCCESS) {
            TaskCompleteMessage complete;
            complete.task_id = msg.task_id;
            complete.worker_id = worker_id_;
            reactor_->send(master_conn_, complete);
            
            if (log) {
                log->info("WorkerAgent", "TaskComplete sent: task_id=" + std::to_string(msg.task_id));
            }
        } else {
            TaskFailedMessage failed;
            failed.task_id = msg.task_id;
            failed.worker_id = worker_id_;
            failed.error_message = result.error;
            reactor_->send(master_conn_, failed);
            
            if (log) {
                log->error("WorkerAgent", "TaskFailed sent: task_id=" + std::to_string(msg.task_id) + ", error=" + result.error);
            }
        }
    }
}

void WorkerAgent::on_shutdown(const ShutdownMessage& msg) {
    auto* log = Logger::get_worker(worker_id_);
    registered_ = false;
    
    if (log) {
        log->info("WorkerAgent", "Shutdown received");
    }
}

}  // namespace fly