#pragma once

#include <storage/cpp/compressor.h>
#include <storage/cpp/pipeline.h>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <streambuf>
#include <vector>

// CompressingStreamBuf —— ostream/sink 适配层（流插件化后为薄壳）：
// 明文字节按 chunk_size 切块，逐块经管线 Stage（压缩 → CRC → 块格式化）
// 交给端点（dest 流式写 / sink 逐块回调）。变换逻辑在
// storage/cpp/pipeline.h 的 Stage 中，本类只做流接口适配与统计转发。
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
    // L1 流式 sink（§9.1 #40）：压缩一块、交付一块（sink = 逐块构造
    // WriteRequest 入 WBQ / 增量写盘 / 网络帧）。
    CompressingStreamBuf(CMUniquePtr<Compressor> compressor,
                         int64_t chunk_size,
                         std::function<void(const char*, size_t)> sink,
                         int64_t compression_threshold = 4096);
    ~CompressingStreamBuf() override;

    int64_t total_uncompressed() const { return pipeline_.total_uncompressed(); }
    int32_t chunk_count() const { return pipeline_.chunk_count(); }
    // 块位置表素材（§14.1 B'）：每块压缩后字节长——trailer 块表的登记来源。
    const CMVector<uint32_t>& block_comp_lens() const { return pipeline_.block_comp_lens(); }
    CompressionType compression_type() const { return comp_type_; }
    // Effective compression type actually applied to the flushed data.
    // All-raw 单块流（旧 skip 语义）→ NONE，否则为配置的压缩类型。
    CompressionType effective_compression_type() const {
        if (pipeline_.chunk_count() > 0 && pipeline_.all_raw()) {
            return CompressionType::NONE;
        }
        return comp_type_;
    }

protected:
    int_type overflow(int_type ch) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;
    int sync() override;

private:
    void emit(const char* data, size_t n);            // 端点交付（sink 或 dest）
    std::ostream* dest_ = nullptr;                    // ostream 模式
    std::function<void(const char*, size_t)> sink_;   // L1 sink 模式
    CompressionType comp_type_ = CompressionType::NONE;
    int64_t chunk_size_;
    int64_t compression_threshold_;
    fly::WritePipeline pipeline_;
};
