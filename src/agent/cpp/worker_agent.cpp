#include <agent/cpp/worker_agent.h>

namespace fly {

WorkerAgent::WorkerAgent(uint64_t worker_id, const CMString& master_host, uint16_t master_port)
    : worker_id_(worker_id), master_host_(master_host), master_port_(master_port), running_(false) {}

WorkerAgent::~WorkerAgent() {
    stop();
}

void WorkerAgent::start() {
    running_ = true;
}

void WorkerAgent::stop() {
    running_ = false;
}

bool WorkerAgent::is_running() const {
    return running_;
}

uint64_t WorkerAgent::get_worker_id() const {
    return worker_id_;
}

}  // namespace fly