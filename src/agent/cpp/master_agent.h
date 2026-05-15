#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

namespace fly {

class MasterAgent {
public:
    MasterAgent(const CMString& host, uint16_t port);
    ~MasterAgent();
    
    void start();
    void stop();
    bool is_running() const;
    
private:
    CMString host_;
    uint16_t port_;
    bool running_;
};

}  // namespace fly