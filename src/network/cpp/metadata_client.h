#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <string>

namespace fly {

// Blocking TCP client for querying Master about data object metadata/locations.
// Thread-safe: each call creates its own socket.
class MetadataClient {
public:
    struct DataLocation {
        bool found_ = false;
        uint64_t worker_id_ = 0;
        CMString host_;
        int32_t port_ = 0;
        CMString error_;
        bool can_still_produce_ = false;
    };

    static DataLocation query_data_location(
        const CMString& master_host,
        int master_port,
        const CMString& object_name,
        int timeout_ms = 5000);
};

}  // namespace fly
