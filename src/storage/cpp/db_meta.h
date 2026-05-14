#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString host;
    CMString role;
    CMString data_path;
    CMString idx_file;
    int64_t idx_entry_count = 0;
    CMString launch_command;

    FLY_SERIALIZE(worker_id, host, role, data_path, idx_file,
                    idx_entry_count, launch_command)
};

struct DbMeta {
    CMString db_id;
    CMString base_path;
    int64_t created_at = 0;
    int64_t frozen_at = 0;
    CMVector<WorkerInfo> workers;

    FLY_SERIALIZE(db_id, base_path, created_at, frozen_at, workers)
};