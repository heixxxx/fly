#include "process_info.h"
#include <unistd.h>

CMSharedPtr<ProcessInfo> ProcessInfo::instance() {
    static CMSharedPtr<ProcessInfo> inst = CMMakeShared<ProcessInfo>();
    return inst;
}

ProcessInfo::ProcessInfo() = default;

CMString ProcessInfo::hostname() const {
    if (!hostname_.empty()) {
        return hostname_;
    }
    char buf[256] = {};
    gethostname(buf, sizeof(buf));
    hostname_ = buf;
    return hostname_;
}

void ProcessInfo::reset() {
    worker_mode_ = false;
    worker_id_ = 0;
    master_port_ = 8000;
    cli_master_port_ = 0;
    master_host_ = "127.0.0.1";
    data_server_host_ = "127.0.0.1";
    script_path_.clear();
    interactive_ = false;
    worker_attributes_.clear();
    hostname_.clear();
}
