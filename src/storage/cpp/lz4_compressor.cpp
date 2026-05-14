#include <storage/cpp/lz4_compressor.h>
#include <lz4.h>
#include <stdexcept>

Lz4Compressor::Lz4Compressor(int acceleration)
    : acceleration_(acceleration) {}

CompressedChunk Lz4Compressor::compress(const CMString& input) {
    CompressedChunk chunk;
    chunk.uncompressed_size = static_cast<int32_t>(input.size());

    if (input.empty()) {
        chunk.compressed_size = 0;
        return chunk;
    }

    int bound = LZ4_compressBound(static_cast<int>(input.size()));
    if (bound <= 0) {
        throw std::runtime_error("LZ4_compressBound failed");
    }

    CMString compressed(static_cast<size_t>(bound), '\0');
    int compressed_size = LZ4_compress_default(
        input.data(),
        compressed.data(),
        static_cast<int>(input.size()),
        bound
    );

    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }

    compressed.resize(static_cast<size_t>(compressed_size));
    chunk.data = std::move(compressed);
    chunk.compressed_size = compressed_size;
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
        throw std::runtime_error("LZ4 decompression failed");
    }

    return output;
}

CompressedChunk Lz4Compressor::compress_chunk(const CMString& input) {
    return compress(input);
}

CMString Lz4Compressor::decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) {
    return decompress(uncompressed_size, compressed_data);
}

CompressionType Lz4Compressor::type() const {
    return CompressionType::LZ4;
}

CMString Lz4Compressor::name() const {
    return "lz4";
}