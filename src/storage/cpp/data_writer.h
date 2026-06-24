#pragma once

#include <storage/cpp/local_index.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/common_types.h>
#include <common/cpp/writer_id.h>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>

// 写入段回滚点：记录 BEGIN 时刻的 data 文件位置，供 ABORT 时 truncate 回去。
struct SegmentRollbackPoint {
    int32_t file_index_ = 1;     // BEGIN 时正在写的 .dat 序号
    int64_t file_offset_ = 0;    // BEGIN 时 current_file_size_
    bool active_ = false;        // 是否有活跃段（已 mark_begin 未 mark_end/abort）
};

class DataWriter {
public:
    DataWriter(
        const CMString& base_path,
        const CMString& data_path,
        const CMString& writer_id,
        int64_t aggregation_threshold,
        const CMString& host = ""
    );

    ~DataWriter();

    DataWriter(const DataWriter&) = delete;
    DataWriter& operator=(const DataWriter&) = delete;

    void write_record(const CMString& object_name,
                        int64_t original_size,
                        int32_t chunk_count,
                        const FlyBuffer& record,
                        const CMString& write_context_hash = "");

    void flush();
    void close();

    // 写入段标记（委托给 LocalIndex）。
    // mark_begin 同时记录 data 文件偏移作为回滚点。
    // mark_end / abort_segment 重置段状态。
    void mark_begin();
    void mark_end();
    // 异常撤销：idx 打 ABORT + data 文件 truncate 回回滚点。
    // 本 db 本 task 无写入（段未开）则 no-op。
    void abort_segment();

    bool segment_active() const { return segment_point_.active_; }

    int64_t total_bytes_written() const;
    int32_t file_count() const;

    std::optional<IndexEntry> get_last_entry(const CMString& object_name);
    std::optional<CMVector<IndexEntry>> get_all_entries(const CMString& object_name);
    bool remove_entry(const CMString& object_name);

private:
    void create_new_file();
    CMString get_current_file_name();
    CMString get_file_name(int32_t index) const;
    // 把 data 文件 truncate 回 segment_point_，并重新打开回滚点 .dat 继续追加。
    void rollback_data_file();

    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString host_;
    int64_t aggregation_threshold_;

    CMString current_file_;
    int32_t file_index_ = 1;
    int64_t current_file_size_ = 0;
    std::ofstream file_stream_;

    CMUniquePtr<LocalIndex> index_;
    int64_t total_bytes_ = 0;
    bool closed_ = false;

    SegmentRollbackPoint segment_point_;
};
