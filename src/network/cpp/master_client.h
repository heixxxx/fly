#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <string>

namespace fly {

// Blocking TCP client for querying Master about data locations.
// Thread-safe: each call creates its own socket.
class MasterClient {
public:
    struct DataLocation {
        bool found = false;
        uint64_t worker_id = 0;
        CMString host;
        int32_t port = 0;
        CMString error;
    };

    static DataLocation query_data_location(
        const CMString& master_host,
        int master_port,
        const CMString& object_name,
        int timeout_ms = 5000);
};

}  // namespace fly
