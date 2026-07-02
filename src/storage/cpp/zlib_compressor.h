#pragma once

#include <storage/cpp/compressor.h>

class ZlibCompressor : public Compressor {
public:
    explicit ZlibCompressor(int level = 6);

    CompressedChunk compress(const CMString& input) override;
    CompressedChunk compress(std::string_view input) override;
    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override;
    int32_t decompress_to(std::string_view compressed_data,
                          char* output, size_t output_size) override;

    CompressionType type() const override;
    CMString name() const override;

private:
    int level_;
};