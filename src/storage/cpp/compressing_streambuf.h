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
    // compression_threshold: payloads at or below this size (in bytes) skip
    // compression and are written as raw passthrough chunks. Lets small
    // objects avoid the compress/decompress overhead. Only applies when the
    // whole payload fits in a single chunk (the common case for small objects,
    // since chunk_size defaults to 4MB). Default 4096 keeps this transparent
    // for existing callers (tests pass it explicitly).
    CompressingStreamBuf(std::ostream& dest, CMUniquePtr<Compressor> compressor,
                         int64_t chunk_size = 4194304,
                         int64_t compression_threshold = 4096);
    ~CompressingStreamBuf() override;

    int64_t total_uncompressed() const { return total_uncompressed_; }
    int32_t chunk_count() const { return chunk_count_; }
    CompressionType compression_type() const { return compressor_ ? compressor_->type() : CompressionType::NONE; }
    // Effective compression type actually applied to the flushed data. Differs
    // from compression_type() only when a small payload skipped compression:
    // then it returns NONE even though a real compressor was configured.
    CompressionType effective_compression_type() const {
        return skipped_ ? CompressionType::NONE : compression_type();
    }

protected:
    int_type overflow(int_type ch) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;
    int sync() override;

private:
    void flush_chunk();

    std::ostream& dest_;
    CMUniquePtr<Compressor> compressor_;
    int64_t chunk_size_;
    int64_t compression_threshold_;
    CMVector<char> buffer_;
    int64_t total_uncompressed_ = 0;
    int32_t chunk_count_ = 0;
    // True once flush_chunk() decided to skip compression for this payload.
    // Set when the first chunk is flushed and the buffered payload is small
    // enough; remains false for larger payloads that actually compressed.
    bool skipped_ = false;
};