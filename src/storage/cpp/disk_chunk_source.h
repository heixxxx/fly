#pragma once

#include <common/io/cpp/chunk_source.h>
#include <container/cpp/container_aliases.h>
#include <cstdint>

namespace fly {

// ── 磁盘拉取源（D3，§14.3）──
//
// low-tier cache 取消后的必要配套：本地流式读（read_streaming 的 TIER1）
// 原依赖缓存共享（SharedMemoryChunkSource 零拷贝整缓冲）——取消后退化为
// 整读盘进内存。DiskChunkSource 以 pread 拉取式消费：块粒度按需读盘，
// 本地读内存有界（块缓冲 + 内核 page cache 天然兜底重复读）。
//
// 块表对账（B'）：构造时已由调用方完成（find_chunked_location 拿区间 +
// META 预解析元数据）；本源只负责字节拉取——块级 CRC 验证在
// DecompressingStreamBuf（消费端逐块验）。
class DiskChunkSource : public ChunkSource {
public:
    // file_path：record 所在 .dat 绝对路径；offset/size：record 区间。
    // py_name/total/chunks/comp_type：来自尾部 trailer 预解析（调用方
    // （read_streaming）读 trailer 一次获得——源不重复解析）。
    DiskChunkSource(CMString file_path, uint64_t offset, uint64_t size,
                    CMString py_name, uint64_t total_uncompressed,
                    uint32_t chunk_count, int comp_type);
    ~DiskChunkSource() override;

    DiskChunkSource(const DiskChunkSource&) = delete;
    DiskChunkSource& operator=(const DiskChunkSource&) = delete;

    int64_t pull(char* dst, size_t n) override;

    const CMString& py_name() const override { return py_name_; }
    uint64_t total_uncompressed() const override { return total_uncompressed_; }
    uint32_t chunk_count() const override { return chunk_count_; }
    int compression_type() const override { return comp_type_; }
    bool failed() const override { return failed_; }
    CMString failure_detail() const override { return failure_detail_; }

private:
    CMString file_path_;
    uint64_t offset_;
    uint64_t size_;
    uint64_t pos_ = 0;
    int fd_ = -1;

    CMString py_name_;
    uint64_t total_uncompressed_ = 0;
    uint32_t chunk_count_ = 0;
    int comp_type_ = -1;
    bool failed_ = false;
    CMString failure_detail_;   // "io: ..."/"integrity: ..."（失败时非空）
};

}  // namespace fly
