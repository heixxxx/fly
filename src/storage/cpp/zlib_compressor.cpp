#include <storage/cpp/zlib_compressor.h>
#include <log/cpp/logger.h>
#include <zlib.h>
#include <stdexcept>

ZlibCompressor::ZlibCompressor(int level)
    : level_(level) {}

CompressedChunk ZlibCompressor::compress(const CMString& input) {
    CompressedChunk chunk;
    chunk.uncompressed_size_ = static_cast<int32_t>(input.size());

    if (input.empty()) {
        chunk.compressed_size_ = 0;
        return chunk;
    }

    uLongf bound = compressBound(static_cast<uLong>(input.size()));
    CMString compressed(static_cast<size_t>(bound), '\0');

    uLongf dest_len = bound;
    int result = compress2(
        reinterpret_cast<Bytef*>(compressed.data()),
        &dest_len,
        reinterpret_cast<const Bytef*>(input.data()),
        static_cast<uLong>(input.size()),
        level_
    );

    if (result != Z_OK) {
        ERR("Zlib compression failed: error {}", result); return {};
    }

    compressed.resize(dest_len);
    chunk.data_ = std::move(compressed);
    chunk.compressed_size_ = static_cast<int32_t>(dest_len);
    return chunk;
}

CMString ZlibCompressor::decompress(int32_t uncompressed_size, const CMString& compressed_data) {
    if (uncompressed_size == 0) {
        return CMString();
    }

    CMString output(static_cast<size_t>(uncompressed_size), '\0');
    uLongf dest_len = static_cast<uLongf>(uncompressed_size);

    int result = uncompress(
        reinterpret_cast<Bytef*>(output.data()),
        &dest_len,
        reinterpret_cast<const Bytef*>(compressed_data.data()),
        static_cast<uLong>(compressed_data.size())
    );

    if (result != Z_OK) {
        ERR("Zlib decompression failed: error {}", result); return {};
    }

    return output;
}

CompressedChunk ZlibCompressor::compress_chunk(const CMString& input) {
    return compress(input);
}

CMString ZlibCompressor::decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) {
    return decompress(uncompressed_size, compressed_data);
}

CompressionType ZlibCompressor::type() const {
    return CompressionType::ZLIB;
}

CMString ZlibCompressor::name() const {
    return "zlib";
}