#pragma once

#include <storage/cpp/compressor.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <fstream>
#include <functional>
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
    // L1 流式 sink（§9.1 #40）：flush_chunk 完成即回调 sink(chunk_view)——
    // "压缩一块、交付一块"（sink = 逐块构造 WriteRequest 入 WBQ / 增量写盘）。
    CompressingStreamBuf(CMUniquePtr<Compressor> compressor,
                         int64_t chunk_size,
                         std::function<void(const char*, size_t)> sink,
                         int64_t compression_threshold = 4096);
    ~CompressingStreamBuf() override;

    int64_t total_uncompressed() const { return total_uncompressed_; }
    int32_t chunk_count() const { return chunk_count_; }
    // 块位置表素材（§14.1 B'）：每块压缩后字节长（flush_chunk 收集）——
    // trailer 块表的登记来源。
    const CMVector<uint32_t>& block_comp_lens() const { return block_comp_lens_; }
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
    void emit(const char* data, size_t n);  // dest_ 或 sink_（二选一）

    std::ostream* dest_ = nullptr;                    // ostream 模式
    std::function<void(const char*, size_t)> sink_;   // L1 sink 模式
    CMUniquePtr<Compressor> compressor_;
    int64_t chunk_size_;
    int64_t compression_threshold_;
    CMVector<char> buffer_;
    int64_t total_uncompressed_ = 0;
    int32_t chunk_count_ = 0;
    CMVector<uint32_t> block_comp_lens_;  // 每块压缩后字节长（B' 块表素材）
    // True once flush_chunk() decided to skip compression for this payload.
    // Set when the first chunk is flushed and the buffered payload is small
    // enough; remains false for larger payloads that actually compressed.
    bool skipped_ = false;
};