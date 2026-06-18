#include <storage/cpp/zstd_compressor.h>
#include <log/cpp/logger.h>
#include <zstd.h>
#include <stdexcept>

ZstdCompressor::ZstdCompressor(int level)
    : level_(level) {}

CompressedChunk ZstdCompressor::compress(const CMString& input) {
    CompressedChunk chunk;
    chunk.uncompressed_size_ = static_cast<int32_t>(input.size());

    if (input.empty()) {
        chunk.compressed_size_ = 0;
        return chunk;
    }

    size_t bound = ZSTD_compressBound(input.size());
    CMString compressed(bound, '\0');

    size_t result = ZSTD_compress(
        compressed.data(),
        bound,
        input.data(),
        input.size(),
        level_
    );

    if (ZSTD_isError(result)) {
        ERR("ZSTD compression failed: {}", ZSTD_getErrorName(result)); return {};
    }

    compressed.resize(result);
    chunk.data_ = std::move(compressed);
    chunk.compressed_size_ = static_cast<int32_t>(result);
    return chunk;
}

CompressedChunk ZstdCompressor::compress(std::string_view input) {
    CompressedChunk chunk;
    chunk.uncompressed_size_ = static_cast<int32_t>(input.size());

    if (input.empty()) {
        chunk.compressed_size_ = 0;
        return chunk;
    }

    size_t bound = ZSTD_compressBound(input.size());
    CMString compressed(bound, '\0');

    size_t result = ZSTD_compress(
        compressed.data(),
        bound,
        input.data(),
        input.size(),
        level_
    );

    if (ZSTD_isError(result)) {
        ERR("ZSTD compression failed: {}", ZSTD_getErrorName(result)); return {};
    }

    compressed.resize(result);
    chunk.data_ = std::move(compressed);
    chunk.compressed_size_ = static_cast<int32_t>(result);
    return chunk;
}

CMString ZstdCompressor::decompress(int32_t uncompressed_size, const CMString& compressed_data) {
    if (uncompressed_size == 0) {
        return CMString();
    }

    CMString output(static_cast<size_t>(uncompressed_size), '\0');

    size_t result = ZSTD_decompress(
        output.data(),
        static_cast<size_t>(uncompressed_size),
        compressed_data.data(),
        compressed_data.size()
    );

    if (ZSTD_isError(result)) {
        ERR("ZSTD decompression failed: {}", ZSTD_getErrorName(result)); return {};
    }

    return output;
}

int32_t ZstdCompressor::decompress_to(std::string_view compressed_data,
                                       char* output, size_t output_size) {
    if (output_size == 0) return 0;

    size_t result = ZSTD_decompress(
        output,
        output_size,
        compressed_data.data(),
        compressed_data.size()
    );

    if (ZSTD_isError(result)) {
        ERR("ZSTD decompress_to failed: {}", ZSTD_getErrorName(result));
        return -1;
    }

    return static_cast<int32_t>(result);
}

CompressedChunk ZstdCompressor::compress_chunk(const CMString& input) {
    return compress(input);
}

CMString ZstdCompressor::decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) {
    return decompress(uncompressed_size, compressed_data);
}

CompressionType ZstdCompressor::type() const {
    return CompressionType::ZSTD;
}

CMString ZstdCompressor::name() const {
    return "zstd";
}