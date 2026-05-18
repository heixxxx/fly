#include <storage/cpp/data_writer.h>
#include <storage/cpp/compression_utils.h>
#include <storage/cpp/compressing_streambuf.h>
#include <filesystem>
#include <stdexcept>
#include <sstream>

namespace fs = std::filesystem;

DataWriter::DataWriter(
    const CMString& base_path,
    const CMString& data_path,
    uint64_t worker_id,
    int64_t aggregation_threshold,
    int64_t large_file_threshold,
    int64_t block_size,
    CompressionType compression_type,
    int64_t compression_threshold,
    int compression_level,
    int64_t stream_chunk_size,
    const CMString& host
)
    : base_path_(base_path)
    , data_path_(data_path)
    , worker_id_(worker_id)
    , host_(host)
    , aggregation_threshold_(aggregation_threshold)
    , large_file_threshold_(large_file_threshold)
    , block_size_(block_size)
    , compression_type_(compression_type)
    , compression_threshold_(compression_threshold)
    , stream_chunk_size_(stream_chunk_size) {

    if (compression_type != CompressionType::NONE) {
        compressor_ = CompressorFactory::create(compression_type);
    }

    fs::create_directories(base_path_);
    CMString write_dir = data_path_.empty() ? base_path_ : data_path_;
    fs::create_directories(write_dir);

    CMString idx_path = base_path_ + "/worker_" + std::to_string(worker_id_) + ".idx";
    index_ = CMMakeUnique<LocalIndex>(idx_path);

    if (fs::exists(idx_path)) {
        index_->load();
    }

    create_new_file();
}

DataWriter::~DataWriter() {
    close();
}

CMString DataWriter::write_object(const CMString& object_name, const CMString& data, bool) {
    if (closed_) {
        throw std::runtime_error("DataWriter is closed");
    }

    if (static_cast<int64_t>(data.size()) >= large_file_threshold_) {
        write_large_object(object_name, data);
    } else {
        write_small_object(object_name, data);
    }

    total_bytes_ += static_cast<int64_t>(data.size());
    return current_file_;
}

CMString DataWriter::write_typed_object(const CMString& object_name, uint64_t original_size,
                                          const CMString& py_name,
                                          const char* data, int64_t data_size) {
    if (closed_) {
        throw std::runtime_error("DataWriter is closed");
    }

    if (current_file_size_ + ObjectHeader::fixed_header_size() +
        static_cast<int64_t>(data_size) > aggregation_threshold_ && current_file_size_ > 0) {
        file_index_++;
        create_new_file();
    }

    int64_t offset = current_file_size_;

    ObjectHeader header;
    header.total_size = original_size;
    header.py_name = py_name;
    header.py_name_len = static_cast<uint16_t>(py_name.size());
    header.compression_type = static_cast<uint8_t>(compression_type_);

    int32_t precomputed_chunks = static_cast<int32_t>(
        (static_cast<int64_t>(data_size) + stream_chunk_size_ - 1) / stream_chunk_size_);
    if (precomputed_chunks < 1) precomputed_chunks = 1;
    header.chunk_count = precomputed_chunks;

    CMString header_bytes = header.serialize();

    CMString compressed_out;
    {
        std::ostringstream oss;
        {
            CompressingStreamBuf buf(oss,
                compressor_ ? CompressorFactory::create(compression_type_) : nullptr,
                stream_chunk_size_);
            std::ostream os(&buf);
            os.write(data, static_cast<std::streamsize>(data_size));
            os.flush();
        }
        compressed_out = oss.str();
    }

    if (current_file_size_ + static_cast<int64_t>(header_bytes.size()) +
        static_cast<int64_t>(compressed_out.size()) > aggregation_threshold_ &&
        current_file_size_ > 0) {
        file_index_++;
        create_new_file();
        offset = 0;
        compressed_out.clear();
        {
            std::ostringstream oss;
            {
                CompressingStreamBuf buf(oss,
                    compressor_ ? CompressorFactory::create(compression_type_) : nullptr,
                    stream_chunk_size_);
                std::ostream os(&buf);
                os.write(data, static_cast<std::streamsize>(data_size));
                os.flush();
            }
            compressed_out = oss.str();
        }
    }

    file_stream_.write(header_bytes.data(), static_cast<std::streamsize>(header_bytes.size()));
    file_stream_.write(compressed_out.data(), static_cast<std::streamsize>(compressed_out.size()));
    current_file_size_ += static_cast<int64_t>(header_bytes.size() + compressed_out.size());

    IndexEntry entry{object_name, current_file_, offset,
                     static_cast<int64_t>(header_bytes.size() + compressed_out.size()),
                     false, precomputed_chunks,
                     static_cast<int8_t>(compression_type_), host_};
    index_->add_entry(entry);

    total_bytes_ += data_size;
    return current_file_;
}

void DataWriter::flush() {
    if (file_stream_.is_open()) {
        file_stream_.flush();
    }
    index_->save();
}

void DataWriter::close() {
    if (closed_) {
        return;
    }
    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }
    index_->save();
    closed_ = true;
}

int64_t DataWriter::total_bytes_written() const {
    return total_bytes_;
}

int32_t DataWriter::file_count() const {
    return file_index_;
}

IndexEntry* DataWriter::get_last_entry(const CMString& object_name) {
    return index_->find_entry(object_name);
}

CMVector<IndexEntry>* DataWriter::get_all_entries(const CMString& object_name) {
    return index_->find_all_entries(object_name);
}

void DataWriter::create_new_file() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }

    current_file_ = get_current_file_name();
    CMString write_dir = data_path_.empty() ? base_path_ : data_path_;
    CMString file_path = write_dir + "/" + current_file_;

    fs::create_directories(write_dir);
    file_stream_.open(file_path, std::ios::binary);
    if (!file_stream_.is_open()) {
        throw std::runtime_error("Failed to create data file: " + file_path);
    }

    current_file_size_ = 0;
}

CMString DataWriter::get_current_file_name() {
    std::ostringstream oss;
    oss << "aggregated_w" << worker_id_ << "_" << std::setfill('0') << std::setw(3) << file_index_ << ".dat";
    return oss.str();
}

void DataWriter::write_small_object(const CMString& object_name, const CMString& data) {
    CMString data_to_write;
    int8_t comp_type = static_cast<int8_t>(CompressionType::NONE);

    if (compressor_ && static_cast<int64_t>(data.size()) >= compression_threshold_) {
        auto chunk = compressor_->compress(data);
        data_to_write = compression_utils::serialize_chunk(chunk);
        comp_type = static_cast<int8_t>(compression_type_);
    } else {
        data_to_write = data;
    }

    if (current_file_size_ + static_cast<int64_t>(data_to_write.size()) > aggregation_threshold_ && current_file_size_ > 0) {
        file_index_++;
        create_new_file();
    }

    int64_t offset = current_file_size_;

    file_stream_.write(data_to_write.data(), static_cast<std::streamsize>(data_to_write.size()));
    current_file_size_ += static_cast<int64_t>(data_to_write.size());

    IndexEntry entry{object_name, current_file_, offset, static_cast<int64_t>(data_to_write.size()), false, 0, comp_type, host_};
    index_->add_entry(entry);
}

void DataWriter::write_large_object(const CMString& object_name, const CMString& data) {
    int64_t total_size = static_cast<int64_t>(data.size());
    int32_t block_count = static_cast<int32_t>((total_size + block_size_ - 1) / block_size_);
    int8_t comp_type = static_cast<int8_t>(compression_type_);

    int64_t offset = 0;
    for (int32_t i = 0; i < block_count; ++i) {
        int64_t remaining = total_size - offset;
        int64_t block_data_size = std::min(remaining, block_size_);
        CMString block_data(data.data() + offset, static_cast<size_t>(block_data_size));

        CMString data_to_write;
        int8_t block_comp_type = static_cast<int8_t>(CompressionType::NONE);

        if (compressor_ && static_cast<int64_t>(block_data.size()) >= compression_threshold_) {
            auto chunk = compressor_->compress_chunk(block_data);
            data_to_write = compression_utils::serialize_chunk(chunk);
            block_comp_type = comp_type;
        } else {
            CompressedChunk raw_chunk;
            raw_chunk.uncompressed_size = static_cast<int32_t>(block_data.size());
            raw_chunk.compressed_size = static_cast<int32_t>(block_data.size());
            raw_chunk.data = block_data;
            data_to_write = compression_utils::serialize_chunk(raw_chunk);
        }

        if (current_file_size_ + static_cast<int64_t>(data_to_write.size()) > aggregation_threshold_ && current_file_size_ > 0) {
            file_index_++;
            create_new_file();
        }

        int64_t file_offset = current_file_size_;
        file_stream_.write(data_to_write.data(), static_cast<std::streamsize>(data_to_write.size()));
        current_file_size_ += static_cast<int64_t>(data_to_write.size());

        IndexEntry block_entry{object_name, current_file_, file_offset, static_cast<int64_t>(data_to_write.size()), true, block_count, block_comp_type, host_};
        index_->add_entry(block_entry);

        offset += block_data_size;
    }
}