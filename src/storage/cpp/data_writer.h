#pragma once

#include <storage/cpp/local_index.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/common_types.h>
#include <common/cpp/writer_id.h>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>

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

    int64_t total_bytes_written() const;
    int32_t file_count() const;

    std::optional<IndexEntry> get_last_entry(const CMString& object_name);
    std::optional<CMVector<IndexEntry>> get_all_entries(const CMString& object_name);
    bool remove_entry(const CMString& object_name);

private:
    void create_new_file();
    CMString get_current_file_name();

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
};
