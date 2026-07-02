#include <storage/cpp/lz4_compressor.h>
#include <log/cpp/logger.h>
#include <lz4.h>
#include <stdexcept>

Lz4Compressor::Lz4Compressor(int acceleration)
    : acceleration_(acceleration) {}

CompressedChunk Lz4Compressor::compress(const CMString& input) {
    CompressedChunk chunk;
    chunk.uncompressed_size_ = static_cast<int32_t>(input.size());

    if (input.empty()) {
        chunk.compressed_size_ = 0;
        return chunk;
    }

    int bound = LZ4_compressBound(static_cast<int>(input.size()));
    if (bound <= 0) {
        ERR("LZ4_compressBound failed"); return {};
    }

    CMString compressed(static_cast<size_t>(bound), '\0');
    int compressed_size = LZ4_compress_default(
        input.data(),
        compressed.data(),
        static_cast<int>(input.size()),
        bound
    );

    if (compressed_size <= 0) {
        ERR("LZ4 compression failed"); return {};
    }

    compressed.resize(static_cast<size_t>(compressed_size));
    chunk.data_ = std::move(compressed);
    chunk.compressed_size_ = compressed_size;
    return chunk;
}

CompressedChunk Lz4Compressor::compress(std::string_view input) {
    CompressedChunk chunk;
    chunk.uncompressed_size_ = static_cast<int32_t>(input.size());

    if (input.empty()) {
        chunk.compressed_size_ = 0;
        return chunk;
    }

    int bound = LZ4_compressBound(static_cast<int>(input.size()));
    if (bound <= 0) {
        ERR("LZ4_compressBound failed"); return {};
    }

    CMString compressed(static_cast<size_t>(bound), '\0');
    int compressed_size = LZ4_compress_default(
        input.data(),
        compressed.data(),
        static_cast<int>(input.size()),
        bound
    );

    if (compressed_size <= 0) {
        ERR("LZ4 compression failed"); return {};
    }

    compressed.resize(static_cast<size_t>(compressed_size));
    chunk.data_ = std::move(compressed);
    chunk.compressed_size_ = compressed_size;
    return chunk;
}

CMString Lz4Compressor::decompress(int32_t uncompressed_size, const CMString& compressed_data) {
    if (uncompressed_size == 0) {
        return CMString();
    }

    CMString output(static_cast<size_t>(uncompressed_size), '\0');
    int result = LZ4_decompress_safe(
        compressed_data.data(),
        output.data(),
        static_cast<int>(compressed_data.size()),
        uncompressed_size
    );

    if (result < 0) {
        ERR("LZ4 decompression failed"); return {};
    }

    return output;
}

int32_t Lz4Compressor::decompress_to(std::string_view compressed_data,
                                      char* output, size_t output_size) {
    if (output_size == 0) return 0;

    int result = LZ4_decompress_safe(
        compressed_data.data(),
        output,
        static_cast<int>(compressed_data.size()),
        static_cast<int>(output_size)
    );

    if (result < 0) {
        ERR("LZ4 decompress_to failed");
        return -1;
    }

    return result;
}

CompressionType Lz4Compressor::type() const {
    return CompressionType::LZ4;
}

CMString Lz4Compressor::name() const {
    return "lz4";
}