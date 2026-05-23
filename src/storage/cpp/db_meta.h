#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

struct WorkerInfo {
    uint64_t worker_id = 0;
    CMString hostname;
    CMString ip_address;
    CMString launch_command;

    FLY_SERIALIZE(worker_id, hostname, ip_address, launch_command)
};

struct DbMetaHeader {
    CMString db_id;
    int64_t created_at = 0;

    FLY_SERIALIZE(db_id, created_at)
};

// Aggregated by load_meta() from DbMetaHeader + [WorkerInfo]...
struct DbMeta {
    CMString db_id;
    int64_t created_at = 0;
    CMVector<WorkerInfo> workers;  // 从增量 WorkerInfo 记录聚合

    // 注意：此结构不直接序列化到 _DB_META
    // 磁盘格式 = DbMetaHeader + [WorkerInfo records]...
    FLY_SERIALIZE(db_id, created_at, workers)
};
