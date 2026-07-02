#include <storage/cpp/compressor.h>
#include <log/cpp/logger.h>
#include <storage/cpp/lz4_compressor.h>
#include <storage/cpp/zlib_compressor.h>
#include <storage/cpp/zstd_compressor.h>

class NoneCompressor : public Compressor {
public:
    CompressedChunk compress(const CMString& input) override {
        CompressedChunk chunk;
        chunk.uncompressed_size_ = static_cast<int32_t>(input.size());
        chunk.data_ = input;
        chunk.compressed_size_ = static_cast<int32_t>(chunk.data_.size());
        return chunk;
    }

    CompressedChunk compress(std::string_view input) override {
        CompressedChunk chunk;
        chunk.uncompressed_size_ = static_cast<int32_t>(input.size());
        chunk.data_ = CMString(input.data(), input.size());
        chunk.compressed_size_ = static_cast<int32_t>(chunk.data_.size());
        return chunk;
    }

    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override {
        (void)uncompressed_size;
        return compressed_data;
    }

    int32_t decompress_to(std::string_view compressed_data,
                          char* output, size_t output_size) override {
        size_t copy_size = std::min(compressed_data.size(), output_size);
        std::memcpy(output, compressed_data.data(), copy_size);
        return static_cast<int32_t>(copy_size);
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
    ERR("Unknown compression type"); return nullptr;
}

CMUniquePtr<Compressor> CompressorFactory::create_from_name(const CMString& name) {
    return create(CompressorFactory::type_from_name(name));
}

CompressionType CompressorFactory::type_from_name(const CMString& name) {
    if (name == "none") return CompressionType::NONE;
    if (name == "lz4") return CompressionType::LZ4;
    if (name == "zlib") return CompressionType::ZLIB;
    if (name == "zstd") return CompressionType::ZSTD;
    ERR("Unknown compression type name: {}", name); return CompressionType::NONE;
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