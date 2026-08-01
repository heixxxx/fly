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
    int64_t created_at_ = 0;

    FLY_SERIALIZE(created_at_)
};

struct DbMeta {
    int64_t created_at_ = 0;
    CMVector<WorkerInfo> workers_;

    FLY_SERIALIZE(created_at_, workers_)
};

// db 迁移标识文件 _MIGRATED_TO 的内容。
//
// merge 跨 path 时（base_path 变化），在源 base_path 根目录写此文件，指向 merge
// 产物路径。源目录保留 _DB_META/_FROZEN/_MIGRATED_TO，删除 .dat/.idx。访问源 path
// 时 DataService::resolve_migrated_path 检测此文件并重定向到 target，让 db_path 作为
// 稳定锚点（源 path 永远存在，通过迁移指针找到最终数据），替代 db_path 的逻辑锚点作用。
//
// 落盘格式同 _DB_META：[8B size][bitsery bytes]。
struct MigrationHeader {
    CMString target_base_path_;   // merge 产物 base_path（idx/_DB_META 所在）
    CMString target_data_path_;   // merge 产物 data_path（.dat 所在）
    int64_t migrated_at_ = 0;     // 迁移时间戳（epoch seconds）

    FLY_SERIALIZE(target_base_path_, target_data_path_, migrated_at_)
};
