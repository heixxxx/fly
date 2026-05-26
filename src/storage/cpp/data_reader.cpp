#include <storage/cpp/data_reader.h>
#include <storage/cpp/compression_utils.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

DataReader::DataReader(
    const CMString& base_path,
    const CMString& data_path,
    const CMString& writer_id
)
    : base_path_(base_path)
    , data_path_(data_path)
    , writer_id_(writer_id) {

    CMString read_dir = data_path_.empty() ? base_path_ : data_path_;
    CMString idx_path = base_path_ + "/" + writer_id_ + ".idx";

    index_ = CMMakeUnique<LocalIndex>(idx_path);
    if (fs::exists(idx_path)) {
        index_->load();
    }
}

DataReader::~DataReader() = default;

ReadResult DataReader::read_object_data(const CMString& object_name) {
    IndexEntry* entry = index_->find_entry(object_name);
    if (!entry) {
        ERR("Object not found: {}", object_name);
        return ReadResult{};
    }
    return read_object_data(*entry);
}

CMString DataReader::read_object(const CMString& object_name) {
    ReadResult result = read_object_data(object_name);
    return CMString(result.data_buffer.begin(), result.data_buffer.end());
}

ReadResult DataReader::read_object_data(const IndexEntry& entry) {
    CMString file_path = find_file_path(entry.file_name);
    CMString raw_data = read_from_file(file_path, entry.offset, entry.size);

    ReadResult result;

    if (raw_data.size() >= 4) {
        uint32_t magic = 0;
        std::memcpy(&magic, raw_data.data(), sizeof(magic));
        if (magic == FLY_OBJECT_MAGIC) {
            int64_t offset = 0;
            ObjectHeader header = ObjectHeader::deserialize(raw_data, offset);

            result.py_name = header.py_name;

            int64_t header_size = offset;
            CMString chunk_data(raw_data.data() + header_size,
                                static_cast<size_t>(entry.size - header_size));

            if (header.compression_type == static_cast<uint8_t>(CompressionType::NONE)) {
                result.data_buffer.assign(
                    chunk_data.data(), chunk_data.data() + static_cast<size_t>(header.total_size));
            } else {
                FlySerBuf decompressed;
                auto compressor = CompressorFactory::create(
                    static_cast<CompressionType>(header.compression_type));

                int64_t chunk_offset = 0;
                while (chunk_offset < static_cast<int64_t>(chunk_data.size())) {
                    auto chunk = compression_utils::deserialize_chunk(chunk_data, chunk_offset);
                    CMString block = compressor->decompress(chunk.uncompressed_size, chunk.data);
                    decompressed.insert(decompressed.end(), block.begin(), block.end());
                }

                result.data_buffer = std::move(decompressed);
            }

            return result;
        }
    }

    if (entry.is_large) {
        CMString large_data = read_large_object(entry);
        result.data_buffer.take(std::move(large_data));
        return result;
    }

    CMString decompressed = decompress_data(raw_data, entry.compression_type);
    result.data_buffer.take(std::move(decompressed));
    return result;
}

bool DataReader::exists(const CMString& object_name) {
    return index_->find_entry(object_name) != nullptr;
}

CMString DataReader::find_file_path(const CMString& file_name) {
    if (!data_path_.empty()) {
        CMString local_path = data_path_ + "/" + file_name;
        if (fs::exists(local_path)) {
            return local_path;
        }
    }

    CMString base_path_file = base_path_ + "/" + file_name;
    if (fs::exists(base_path_file)) {
        return base_path_file;
    }

    ERR("Data file not found: {}", file_name);
    return {};
}

CMString DataReader::read_from_file(const CMString& file_path, int64_t offset, int64_t size) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        ERR("Failed to open data file: {}", file_path);
        return {};
    }

    ifs.seekg(offset);
    CMString buffer(static_cast<size_t>(size), '\0');
    ifs.read(buffer.data(), static_cast<std::streamsize>(size));

    if (!ifs) {
        ERR("Failed to read data from file: {}", file_path);
        return {};
    }

    return buffer;
}

CMString DataReader::decompress_data(const CMString& raw_data, int8_t compression_type) {
    if (compression_type == static_cast<int8_t>(CompressionType::NONE)) {
        if (raw_data.size() >= sizeof(int32_t) * 2) {
            int32_t uncompressed_size = 0;
            int32_t compressed_size = 0;
            std::memcpy(&uncompressed_size, raw_data.data(), sizeof(int32_t));
            std::memcpy(&compressed_size, raw_data.data() + sizeof(int32_t), sizeof(int32_t));

            int64_t expected_total = sizeof(int32_t) * 2 + static_cast<int64_t>(compressed_size);
            if (expected_total == static_cast<int64_t>(raw_data.size())) {
                int64_t offset = 0;
                auto chunk = compression_utils::deserialize_chunk(CMString(raw_data), offset);
                return chunk.data;
            }
        }
        return raw_data;
    }

    int64_t offset = 0;
    auto chunk = compression_utils::deserialize_chunk(CMString(raw_data), offset);

    auto compressor = CompressorFactory::create(static_cast<CompressionType>(compression_type));
    return compressor->decompress(chunk.uncompressed_size, chunk.data);
}

CMString DataReader::read_large_object(const IndexEntry& first_entry) {
    CMString object_name = first_entry.object_name;
    CMVector<IndexEntry>* all_blocks = index_->find_all_entries(object_name);
    if (!all_blocks || all_blocks->empty()) {
        ERR("No blocks found for large object: {}", object_name);
        return {};
    }

    CMVector<IndexEntry> blocks = *all_blocks;

    std::sort(blocks.begin(), blocks.end(),
        [](const IndexEntry& a, const IndexEntry& b) {
            if (a.file_name != b.file_name) return a.file_name < b.file_name;
            return a.offset < b.offset;
        });

    CMString result;
    for (const auto& block : blocks) {
        CMString file_path = find_file_path(block.file_name);
        CMString raw_data = read_from_file(file_path, block.offset, block.size);
        CMString decompressed = decompress_data(raw_data, block.compression_type);
        result += decompressed;
    }

    return result;
}

ReadResult DataReader::read_from_entries(const CMVector<IndexEntry>& entries) {
    if (entries.empty()) {
        ERR("No entries to read");
        return ReadResult{};
    }

    if (entries.size() == 1) {
        return read_object_data(entries.front());
    }

    CMVector<IndexEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
        [](const IndexEntry& a, const IndexEntry& b) {
            if (a.file_name != b.file_name) return a.file_name < b.file_name;
            return a.offset < b.offset;
        });

    FlySerBuf result;
    CMString py_name;

    for (size_t i = 0; i < sorted.size(); ++i) {
        const auto& block = sorted[i];
        CMString file_path = find_file_path(block.file_name);
        CMString raw_data = read_from_file(file_path, block.offset, block.size);

        if (i == 0 && raw_data.size() >= 4) {
            uint32_t magic = 0;
            std::memcpy(&magic, raw_data.data(), sizeof(magic));
            if (magic == FLY_OBJECT_MAGIC) {
                int64_t offset = 0;
                ObjectHeader header = ObjectHeader::deserialize(raw_data, offset);
                py_name = header.py_name;

                CMString chunk_data(raw_data.data() + offset,
                                    static_cast<size_t>(block.size - offset));
                CMString decompressed = decompress_data(chunk_data, block.compression_type);
                result.insert(result.end(), decompressed.begin(), decompressed.end());
                continue;
            }
        }

        CMString decompressed = decompress_data(raw_data, block.compression_type);
        result.insert(result.end(), decompressed.begin(), decompressed.end());
    }

    ReadResult rr;
    rr.py_name = py_name;
    rr.data_buffer = std::move(result);
    return rr;
}