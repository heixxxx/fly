#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <memory>
#include <stdexcept>

enum class CompressionType : int8_t {
    NONE = 0,
    LZ4 = 1,
    ZLIB = 2,
    ZSTD = 3,
};

// On-disk format for each stored chunk:
// [int32_t uncompressed_size][int32_t compressed_size][compressed_bytes...]
struct CompressedChunk {
    int32_t uncompressed_size = 0;
    int32_t compressed_size = 0;
    CMString data;
};

class Compressor {
public:
    virtual ~Compressor() = default;

    virtual CompressedChunk compress(const CMString& input) = 0;
    virtual CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) = 0;

    // Each call produces an independently decompressible block for streaming.
    virtual CompressedChunk compress_chunk(const CMString& input) = 0;
    virtual CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) = 0;

    virtual CompressionType type() const = 0;
    virtual CMString name() const = 0;
};

class CompressorFactory {
public:
    static CMUniquePtr<Compressor> create(CompressionType type);
    static CMUniquePtr<Compressor> create_from_name(const CMString& name);

    static CompressionType type_from_name(const CMString& name);
    static CMString name_from_type(CompressionType type);
};