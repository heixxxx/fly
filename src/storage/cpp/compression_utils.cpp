#include <storage/cpp/compression_utils.h>
#include <log/cpp/logger.h>
#include <cstring>
#include <stdexcept>

namespace compression_utils {

CMString serialize_chunk(const CompressedChunk& chunk) {
    int32_t uncompressed_size = chunk.uncompressed_size;
    int32_t compressed_size = chunk.compressed_size;

    CMString result;
    result.resize(sizeof(int32_t) * 2 + static_cast<size_t>(compressed_size));

    std::memcpy(result.data(), &uncompressed_size, sizeof(int32_t));
    std::memcpy(result.data() + sizeof(int32_t), &compressed_size, sizeof(int32_t));
    std::memcpy(result.data() + sizeof(int32_t) * 2, chunk.data.data(), static_cast<size_t>(compressed_size));

    return result;
}

CompressedChunk deserialize_chunk(const CMString& data, int64_t& offset) {
    CompressedChunk chunk;

    if (static_cast<int64_t>(data.size()) < offset + static_cast<int64_t>(sizeof(int32_t) * 2)) {
        ERR("Insufficient data for chunk header"); return {};
    }

    std::memcpy(&chunk.uncompressed_size, data.data() + offset, sizeof(int32_t));
    std::memcpy(&chunk.compressed_size, data.data() + offset + sizeof(int32_t), sizeof(int32_t));
    offset += sizeof(int32_t) * 2;

    if (static_cast<int64_t>(data.size()) < offset + static_cast<int64_t>(chunk.compressed_size)) {
        ERR("Insufficient data for chunk payload"); return {};
    }

    chunk.data.assign(data.data() + offset, static_cast<size_t>(chunk.compressed_size));
    offset += static_cast<int64_t>(chunk.compressed_size);

    return chunk;
}

int64_t write_compressed_to_stream(const CompressedChunk& chunk, std::ofstream& ofs) {
    int32_t uncompressed_size = chunk.uncompressed_size;
    int32_t compressed_size = chunk.compressed_size;

    ofs.write(reinterpret_cast<const char*>(&uncompressed_size), sizeof(int32_t));
    ofs.write(reinterpret_cast<const char*>(&compressed_size), sizeof(int32_t));
    ofs.write(chunk.data.data(), static_cast<std::streamsize>(compressed_size));

    return sizeof(int32_t) * 2 + static_cast<int64_t>(compressed_size);
}

CompressedChunk read_compressed_from_stream(std::ifstream& ifs, int64_t offset) {
    CompressedChunk chunk;

    ifs.seekg(offset);

    ifs.read(reinterpret_cast<char*>(&chunk.uncompressed_size), sizeof(int32_t));
    ifs.read(reinterpret_cast<char*>(&chunk.compressed_size), sizeof(int32_t));

    chunk.data.resize(static_cast<size_t>(chunk.compressed_size));
    ifs.read(chunk.data.data(), static_cast<std::streamsize>(chunk.compressed_size));

    return chunk;
}

}