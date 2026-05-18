#pragma once

#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <memory>
#include <streambuf>
#include <vector>

class CompressingStreamBuf : public std::streambuf {
public:
    CompressingStreamBuf(std::ostream& dest, CMUniquePtr<Compressor> compressor,
                         int64_t chunk_size = 4194304);
    ~CompressingStreamBuf() override;

    int64_t total_uncompressed() const { return total_uncompressed_; }
    int32_t chunk_count() const { return chunk_count_; }
    CompressionType compression_type() const { return compressor_ ? compressor_->type() : CompressionType::NONE; }

protected:
    int_type overflow(int_type ch) override;
    int sync() override;

private:
    void flush_chunk();

    std::ostream& dest_;
    CMUniquePtr<Compressor> compressor_;
    int64_t chunk_size_;
    CMVector<char> buffer_;
    int64_t total_uncompressed_ = 0;
    int32_t chunk_count_ = 0;
};