#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString writer_id;
    CMString hostname;
    CMString ip_address;
    CMString launch_command;

    FLY_SERIALIZE(worker_id, writer_id, hostname, ip_address, launch_command)
};

struct DbMetaHeader {
    CMString db_id;
    int64_t created_at = 0;

    FLY_SERIALIZE(db_id, created_at)
};

struct DbMeta {
    CMString db_id;
    int64_t created_at = 0;
    CMVector<WorkerInfo> workers;

    FLY_SERIALIZE(db_id, created_at, workers)
};
