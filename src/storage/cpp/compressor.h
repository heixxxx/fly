#pragma once

#include <container/cpp/container_aliases.h>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>

enum class CompressionType : int8_t {
    NONE = 0,
    LZ4 = 1,
    ZLIB = 2,
    ZSTD = 3,
};

// On-disk format for each stored chunk:
// [int32_t uncompressed_size][int32_t compressed_size][compressed_bytes...]
struct CompressedChunk {
    int32_t uncompressed_size_ = 0;
    int32_t compressed_size_ = 0;
    CMString data_;
};

class Compressor {
public:
    virtual ~Compressor() = default;

    virtual CompressedChunk compress(const CMString& input) = 0;

    // Zero-copy compress: accepts string_view input, avoids intermediate CMString.
    virtual CompressedChunk compress(std::string_view input) = 0;

    virtual CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) = 0;

    // Zero-copy decompress: directly write to target buffer, avoid intermediate CMString.
    // Returns number of bytes written, or -1 on error.
    virtual int32_t decompress_to(std::string_view compressed_data,
                                  char* output, size_t output_size) = 0;

    virtual CompressionType type() const = 0;
    virtual CMString name() const = 0;
};

class CompressorFactory {
public:
    // level < 0 时用各 Compressor 的默认级别（Zlib=6 / Zstd=3 / Lz4=1），
    // 否则透传给具体 Compressor（Zlib/Zstd: 压缩级别；Lz4: acceleration）。
    static CMUniquePtr<Compressor> create(CompressionType type, int level = -1);
    static CMUniquePtr<Compressor> create_from_name(const CMString& name);

    static CompressionType type_from_name(const CMString& name);
    static CMString name_from_type(CompressionType type);
};