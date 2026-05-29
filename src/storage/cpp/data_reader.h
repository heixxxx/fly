#pragma once

#include <storage/cpp/local_index.h>
#include <storage/cpp/compressor.h>
#include <serialization/cpp/object_header.h>
#include <serialization/cpp/serialization_macros.h>
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

    template<typename T>
    CMSharedPtr<T> read_object(const CMString& object_name) {
        IndexEntry* entry = index_->find_entry(object_name);
        if (!entry) {
            ERR("Object not found: {}", object_name);
            return nullptr;
        }
        return read_object_entry<T>(*entry);
    }

    template<typename T>
    CMSharedPtr<T> read_object_entry(const IndexEntry& entry) {
        ReadResult result = read_object_data(entry);
        auto obj = CMMakeShared<T>();
        FLY_DECODE_FROM_BYTES(result.data_buffer, T, *obj);
        return obj;
    }

    CMString read_object(const CMString& object_name);
    ReadResult read_object_data(const CMString& object_name);
    ReadResult read_object_data(const IndexEntry& entry);
    CMString decompress_data(const CMString& raw_data, int8_t compression_type);

    CMString read_raw_bytes(const CMString& object_name);
    CMString read_raw_bytes(const IndexEntry& entry);

    bool exists(const CMString& object_name);

    ReadResult read_from_entries(const CMVector<IndexEntry>& entries);

private:
    CMString find_file_path(const CMString& file_name);
    CMString read_from_file(const CMString& file_path, int64_t offset, int64_t size);
    CMString read_large_object(const IndexEntry& entry);

    CMString base_path_;
    CMString data_path_;
    CMString writer_id_;

    CMUniquePtr<LocalIndex> index_;
};