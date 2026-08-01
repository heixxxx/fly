#pragma once

#include <storage/cpp/local_index.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/common_types.h>
#include <log/cpp/logger.h>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>

struct ReadResult {
    FlySerBuf data_buffer_;
    CMString py_name_;
    bool can_still_produce_ = false;
};

class DataReader {
public:
    DataReader(
        const CMString& db_path,
        const CMString& data_path,
        const CMString& writer_id
    );

    ~DataReader();

    DataReader(const DataReader&) = delete;
    DataReader& operator=(const DataReader&) = delete;

    FlyBufferPtr read_raw_bytes(const CMString& object_name);
    FlyBufferPtr read_raw_bytes(const IndexEntry& entry);

    bool exists(const CMString& object_name);

    CMString find_file_path(const CMString& file_name);
    std::optional<IndexEntry> find_entry(const CMString& object_name);
    std::optional<CMVector<IndexEntry>> find_all_entries(const CMString& object_name);

private:
    FlyBufferPtr read_from_file(const CMString& file_path, int64_t offset, int64_t size);

    CMString db_path_;
    CMString data_path_;
    CMString writer_id_;

    CMUniquePtr<LocalIndex> index_;
};
