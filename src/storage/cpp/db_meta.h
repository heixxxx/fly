#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

struct WorkerInfo {
    uint64_t worker_id_ = 0;
    CMString writer_id_;
    CMString hostname_;
    CMString ip_address_;
    CMString launch_command_;

    FLY_SERIALIZE(worker_id_, writer_id_, hostname_, ip_address_, launch_command_)
};

struct DbMetaHeader {
    CMString db_id_;
    int64_t created_at_ = 0;

    FLY_SERIALIZE(db_id_, created_at_)
};

struct DbMeta {
    CMString db_id_;
    int64_t created_at_ = 0;
    CMVector<WorkerInfo> workers_;

    FLY_SERIALIZE(db_id_, created_at_, workers_)
};
