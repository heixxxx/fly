#include <agent/cpp/master_agent.h>

namespace fly {

MasterAgent::MasterAgent(const CMString& host, uint16_t port)
    : host_(host), port_(port), running_(false) {}

MasterAgent::~MasterAgent() {
    stop();
}

void MasterAgent::start() {
    running_ = true;
}

void MasterAgent::stop() {
    running_ = false;
}

bool MasterAgent::is_running() const {
    return running_;
}

}  // namespace fly