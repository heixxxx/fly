#include <storage/cpp/compressor.h>
#include <storage/cpp/lz4_compressor.h>
#include <storage/cpp/zlib_compressor.h>
#include <storage/cpp/zstd_compressor.h>

class NoneCompressor : public Compressor {
public:
    CompressedChunk compress(const CMString& input) override {
        CompressedChunk chunk;
        chunk.uncompressed_size = static_cast<int32_t>(input.size());
        chunk.data = input;
        chunk.compressed_size = static_cast<int32_t>(chunk.data.size());
        return chunk;
    }

    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override {
        (void)uncompressed_size;
        return compressed_data;
    }

    CompressedChunk compress_chunk(const CMString& input) override {
        return compress(input);
    }

    CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) override {
        return decompress(uncompressed_size, compressed_data);
    }

    CompressionType type() const override { return CompressionType::NONE; }
    CMString name() const override { return "none"; }
};

CMUniquePtr<Compressor> CompressorFactory::create(CompressionType type) {
    switch (type) {
        case CompressionType::NONE:
            return CMMakeUnique<NoneCompressor>();
        case CompressionType::LZ4:
            return CMMakeUnique<Lz4Compressor>();
        case CompressionType::ZLIB:
            return CMMakeUnique<ZlibCompressor>();
        case CompressionType::ZSTD:
            return CMMakeUnique<ZstdCompressor>();
    }
    throw std::runtime_error("Unknown compression type");
}

CMUniquePtr<Compressor> CompressorFactory::create_from_name(const CMString& name) {
    return create(CompressorFactory::type_from_name(name));
}

CompressionType CompressorFactory::type_from_name(const CMString& name) {
    if (name == "none") return CompressionType::NONE;
    if (name == "lz4") return CompressionType::LZ4;
    if (name == "zlib") return CompressionType::ZLIB;
    if (name == "zstd") return CompressionType::ZSTD;
    throw std::runtime_error("Unknown compression type name: " + name);
}

CMString CompressorFactory::name_from_type(CompressionType type) {
    switch (type) {
        case CompressionType::NONE: return "none";
        case CompressionType::LZ4: return "lz4";
        case CompressionType::ZLIB: return "zlib";
        case CompressionType::ZSTD: return "zstd";
    }
    return "unknown";
}