#pragma once

#include <storage/cpp/local_index.h>
#include <storage/cpp/compressor.h>
#include <serialization/cpp/object_header.h>
#include <serialization/cpp/serialization_macros.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <memory>

class DataWriter {
public:
    DataWriter(
        const CMString& base_path,
        const CMString& data_path,
        uint64_t worker_id,
        int64_t aggregation_threshold,
        int64_t large_file_threshold,
        int64_t block_size,
        CompressionType compression_type = CompressionType::LZ4,
        int64_t compression_threshold = 128,
        int compression_level = 0,
        int64_t stream_chunk_size = 4194304
    );

    ~DataWriter();

    DataWriter(const DataWriter&) = delete;
    DataWriter& operator=(const DataWriter&) = delete;

    template<typename T>
    CMString write_object(const CMString& object_name, const T& obj,
                           const CMString& py_name = "") {
        FlyBuffer buffer;
        FLY_ENCODE_TO_BYTES(obj, buffer);
        return write_typed_object(object_name, static_cast<uint64_t>(buffer.size()),
                                  py_name, reinterpret_cast<const char*>(buffer.data()),
                                  static_cast<int64_t>(buffer.size()));
    }

    CMString write_object(const CMString& object_name, const CMString& data, bool backup = false);

    CMString write_typed_object(const CMString& object_name, uint64_t original_size,
                                 const CMString& py_name,
                                 const char* data, int64_t data_size);

    void flush();
    void close();

    int64_t total_bytes_written() const;
    int32_t file_count() const;

private:
    void create_new_file();
    CMString get_current_file_name();
    void write_small_object(const CMString& object_name, const CMString& data);
    void write_large_object(const CMString& object_name, const CMString& data);

    CMString base_path_;
    CMString data_path_;
    uint64_t worker_id_;
    int64_t aggregation_threshold_;
    int64_t large_file_threshold_;
    int64_t block_size_;

    CompressionType compression_type_;
    int64_t compression_threshold_;
    std::unique_ptr<Compressor> compressor_;
    int64_t stream_chunk_size_;

    CMString current_file_;
    int32_t file_index_ = 1;
    int64_t current_file_size_ = 0;
    std::ofstream file_stream_;

    std::unique_ptr<LocalIndex> index_;
    int64_t total_bytes_ = 0;
    bool closed_ = false;
};