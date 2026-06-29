#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <string>

namespace fly {

class Transport;

// A single replica location returned by master.
struct ReplicaLocation {
    uint64_t worker_id_ = 0;
    CMString host_;
    int32_t port_ = 0;
};

class MetadataClient {
public:
    // Result of query_data_location. all_locations_ holds every replica master
    // knows about (authoritative when found_). The single worker_id_/host_/port_
    // fields mirror all_locations_[0] for convenience when only one replica is
    // needed.
    struct DataLocation {
        bool found_ = false;
        uint64_t worker_id_ = 0;
        CMString host_;
        int32_t port_ = 0;
        CMString error_;
        bool can_still_produce_ = false;
        CMVector<ReplicaLocation> all_locations_;
    };

    explicit MetadataClient(CMSharedPtr<Transport> transport);
    MetadataClient();

    DataLocation query_data_location(
        const CMString& master_host,
        int master_port,
        const CMString& object_name,
        int timeout_ms = 5000);

private:
    CMSharedPtr<Transport> transport_;
};

}  // namespace fly
