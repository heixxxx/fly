#pragma once

#include <storage/cpp/local_index.h>
#include <storage/cpp/compressor.h>
#include <serialization/cpp/object_header.h>
#include <serialization/cpp/serialization_macros.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <memory>

struct ReadResult {
    FlyBuffer data_buffer;
    CMString py_name;
};

class DataReader {
public:
    DataReader(
        const CMString& base_path,
        const CMString& data_path,
        uint64_t worker_id
    );

    ~DataReader();

    DataReader(const DataReader&) = delete;
    DataReader& operator=(const DataReader&) = delete;

    template<typename T>
    std::shared_ptr<T> read_object(const CMString& object_name) {
        IndexEntry* entry = index_->find_entry(object_name);
        if (!entry) {
            throw std::runtime_error("Object not found: " + object_name);
        }
        return read_object_entry<T>(*entry);
    }

    template<typename T>
    std::shared_ptr<T> read_object_entry(const IndexEntry& entry) {
        ReadResult result = read_object_data(entry);
        auto obj = std::make_shared<T>();
        FLY_DECODE_FROM_BYTES(result.data_buffer, T, *obj);
        return obj;
    }

    CMString read_object(const CMString& object_name);
    ReadResult read_object_data(const CMString& object_name);
    ReadResult read_object_data(const IndexEntry& entry);

    bool exists(const CMString& object_name);

    ReadResult read_from_entries(const CMVector<IndexEntry>& entries);

private:
    CMString find_file_path(const CMString& file_name);
    CMString read_from_file(const CMString& file_path, int64_t offset, int64_t size);
    CMString decompress_data(const CMString& raw_data, int8_t compression_type);
    CMString read_large_object(const IndexEntry& entry);

    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;

    std::unique_ptr<LocalIndex> index_;
};