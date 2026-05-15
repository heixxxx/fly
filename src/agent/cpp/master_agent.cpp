#include <agent/cpp/master_agent.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), running_(false) {}

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
    
    reactor_->on_disconnect([this](uint64_t conn_id) {
        on_disconnect(conn_id);
    });
    
    reactor_->on_error([this](uint64_t conn_id, int err) {
        on_error(conn_id, err);
    });
    
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
        reactor_->stop();
        if (reactor_thread_.joinable()) {
            reactor_thread_.join();
        }
        reactor_.reset();
        
        conn_to_worker_.clear();
        worker_to_conn_.clear();
        
        running_ = false;
    }
}

bool MasterAgent::is_running() const {
    return running_;
}

void MasterAgent::on_worker_register(uint64_t conn_id, const RegisterMessage& msg) {
    uint64_t worker_id = msg.worker_id;
    
    conn_to_worker_[conn_id] = worker_id;
    worker_to_conn_[worker_id] = conn_id;
    
    auto* log = Logger::get_master();
    if (log) {
        log->info("MasterAgent", "Worker registered: conn_id=" + std::to_string(conn_id) + ", worker_id=" + std::to_string(worker_id));
    }
    
    RegisterAckMessage ack;
    ack.worker_id = worker_id;
    ack.master_address = host_;
    ack.master_port = static_cast<int32_t>(port_);
    reactor_->send(conn_id, ack);
}

void MasterAgent::on_heartbeat(uint64_t conn_id, const HeartbeatMessage& msg) {
    auto* log = Logger::get_master();
    if (log) {
        log->debug("MasterAgent", "Heartbeat from worker_id=" + std::to_string(msg.worker_id));
    }
}

void MasterAgent::on_disconnect(uint64_t conn_id) {
    auto* log = Logger::get_master();
    
    auto it = conn_to_worker_.find(conn_id);
    if (it != conn_to_worker_.end()) {
        uint64_t worker_id = it->second;
        conn_to_worker_.erase(conn_id);
        worker_to_conn_.erase(worker_id);
        
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

}  // namespace fly