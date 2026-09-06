#pragma once

#include <common/io/cpp/chunk_source.h>
#include <container/cpp/container_aliases.h>
#include <common/buffer/cpp/fly_buffer.h>
#include <cstdint>

namespace fly {

// 内存源：record 全量（块流 + trailer）等价迁移（原 DecompressingStreamBuf
// (data,size) 的行为）。构造时尾部解析 trailer 拿元数据与块流边界。
class MemoryChunkSource : public ChunkSource {
public:
    explicit MemoryChunkSource(const char* data, size_t size);

    int64_t pull(char* dst, size_t n) override;

    const CMString& py_name() const override { return py_name_; }
    uint64_t total_uncompressed() const override { return total_uncompressed_; }
    uint32_t chunk_count() const override { return chunk_count_; }
    int compression_type() const override { return compression_type_; }
    bool failed() const override { return failed_; }
    CMString failure_detail() const override { return failure_detail_; }

    // 块流区域长度（trailer 解析结果；解析失败时 0 且 failed_=true）
    uint64_t block_area_len() const { return block_area_len_; }

protected:
    // 共享基类字段（SharedMemoryChunkSource 复用解析逻辑）。
    void parse_trailer(const char* data, size_t size);

    const char* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    uint64_t block_area_len_ = 0;
    CMString py_name_;
    uint64_t total_uncompressed_ = 0;
    uint32_t chunk_count_ = 0;
    int compression_type_ = -1;
    bool failed_ = false;
    CMString failure_detail_;   // "integrity: ..."（解析失败时非空）
};

// 持有所有权版（FlyBufferPtr 生命周期随源）：TIER1 命中路径用——raw 缓冲
// 由源独占持有，调用方无需延长生命周期。
class SharedMemoryChunkSource : public MemoryChunkSource {
public:
    explicit SharedMemoryChunkSource(const char* data, size_t size, FlyBufferPtr owner)
        : MemoryChunkSource(data, size), owner_(std::move(owner)) {}

private:
    FlyBufferPtr owner_;
};

}  // namespace fly
