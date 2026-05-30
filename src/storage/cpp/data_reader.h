#pragma once

#include <storage/cpp/local_index.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/common_types.h>
#include <log/cpp/logger.h>
#include <cstdint>
#include <fstream>
#include <memory>

struct ReadResult {
    FlySerBuf data_buffer;
    CMString py_name;
};

class DataReader {
public:
    DataReader(
        const CMString& base_path,
        const CMString& data_path,
        const CMString& writer_id
    );

    ~DataReader();

    DataReader(const DataReader&) = delete;
    DataReader& operator=(const DataReader&) = delete;

    CMString read_raw_bytes(const CMString& object_name);
    CMString read_raw_bytes(const IndexEntry& entry);

    bool exists(const CMString& object_name);

    CMString find_file_path(const CMString& file_name);
    IndexEntry* find_entry(const CMString& object_name);
    CMVector<IndexEntry>* find_all_entries(const CMString& object_name);

private:
    CMString read_from_file(const CMString& file_path, int64_t offset, int64_t size);

    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;

    CMUniquePtr<LocalIndex> index_;
};
