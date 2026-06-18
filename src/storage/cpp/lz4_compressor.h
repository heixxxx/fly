#pragma once

#include <storage/cpp/compressor.h>

class Lz4Compressor : public Compressor {
public:
    explicit Lz4Compressor(int acceleration = 1);

    CompressedChunk compress(const CMString& input) override;
    CompressedChunk compress(std::string_view input) override;
    CMString decompress(int32_t uncompressed_size, const CMString& compressed_data) override;
    int32_t decompress_to(std::string_view compressed_data,
                          char* output, size_t output_size) override;
    CompressedChunk compress_chunk(const CMString& input) override;
    CMString decompress_chunk(int32_t uncompressed_size, const CMString& compressed_data) override;

    CompressionType type() const override;
    CMString name() const override;

private:
    int acceleration_;
};