#pragma once

#include <storage/cpp/compressor.h>

class ZlibCompressor : public Compressor {
public:
    explicit ZlibCompressor(int level = 6);

    CompressedChunk compress(const CMString& input) override;
    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override;
    CompressedChunk compress_chunk(const CMString& input) override;
    CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) override;

    CompressionType type() const override;
    CMString name() const override;

private:
    int level_;
};