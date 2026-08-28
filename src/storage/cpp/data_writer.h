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
    // temp_mode：temp 专用 writer——数据文件 temp_data_{wid}_{NNN}.dat、
    // idx 文件 {wid}.temp.idx（op-log 事务段格式同正式 idx），写目录恒
    // db_path（temp 落盘必须 db 目录内自包含，支撑 task 级断点跨进程恢复
    // 与 project 整体迁移）。db freeze 后 temp 文件全部删除。
    DataWriter(
        const CMString& db_path,
        const CMString& data_path,
        const CMString& writer_id,
        int64_t aggregation_threshold,
        const CMString& host = "",
        bool temp_mode = false
    );

    ~DataWriter();

    DataWriter(const DataWriter&) = delete;
    DataWriter& operator=(const DataWriter&) = delete;

    void write_record(const CMString& object_name,
                        int64_t original_size,
                        int32_t chunk_count,
                        const FlyBuffer& record,
                        const CMString& write_context_hash = "");

    // ── L1 增量写（chunked-transfer-design §9.1 #39）──
    // 大对象块流纯追加：begin（可能先滚文件——当前过半即滚，增量写不跨
    // 文件，IndexEntry 单文件区间约束）→ append×N（含末尾 trailer——由
    // 调用方通过 append 写入）→ finish 登记 entry。
    // 段事务兼容：begin 在活跃段内（mark_write_begin 后），abort 段 truncate
    // 残块——无 trailer 结构上不可读（commit marker 语义，§4.4）。
    // begin 返回 record 起点偏移（-1 = writer 已关闭）。调用序列必须在同一线程
    //（WBQ 单消费线程保序，块顺序 = record 字节序）。
    int64_t begin_incremental();
    void append_incremental(const char* data, size_t n);
    void finish_incremental(const CMString& object_name, int64_t original_size,
                            int32_t chunk_count,
                            const CMString& write_context_hash = "");

    // 落盘 + 检查流状态。返回 false 表示 write/flush 失败（磁盘满/IO 错误），
    // 调用方应据此标记对象写入失败而非错误地标记 COMPLETE。
    bool write_record_checked(const CMString& object_name,
                        int64_t original_size,
                        int32_t chunk_count,
                        const FlyBuffer& record,
                        const CMString& write_context_hash = "");

    void flush();

    // flush + 检查流状态。返回 false 表示 flush 失败。
    bool flush_checked();
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
    CMString writer_id() const { return writer_id_; }

    std::optional<IndexEntry> get_last_entry(const CMString& object_name);
    std::optional<CMVector<IndexEntry>> get_all_entries(const CMString& object_name);
    bool remove_entry(const CMString& object_name);

private:
    void create_new_file();
    CMString get_current_file_name();
    CMString get_file_name(int32_t index) const;
    // 把 data 文件 truncate 回 segment_point_，并重新打开回滚点 .dat 继续追加。
    void rollback_data_file();

    CMString db_path_;
    CMString data_path_;
    CMString writer_id_;
    CMString host_;
    int64_t aggregation_threshold_;
    bool temp_mode_ = false;

    CMString current_file_;
    int32_t file_index_ = 1;
    int64_t current_file_size_ = 0;
    std::ofstream file_stream_;

    CMUniquePtr<LocalIndex> index_;
    int64_t total_bytes_ = 0;
    bool closed_ = false;

    SegmentRollbackPoint segment_point_;

    // L1 增量写状态（-1 = 无活跃增量 record）
    int64_t incremental_offset_ = -1;
    CMString incremental_file_;
};
