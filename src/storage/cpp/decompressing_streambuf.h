#pragma once

#include <storage/cpp/compressor.h>
#include <storage/cpp/pipeline.h>
#include <common/cpp/chunk_source.h>
#include <common/cpp/common_types.h>
#include <cstdint>
#include <memory>
#include <streambuf>
#include <vector>

// DecompressingStreamBuf — read-side counterpart of CompressingStreamBuf.
//
// Input format (trailer layout, chunked-transfer-design.md §4.4):
//   [Chunk1][Chunk2]...[ChunkN][trailer_header][u64 trailer_crc]
// Each chunk:   [int32_t uncompressed_size][int32_t compressed_size][uint64_t crc][data...]
// trailer:      ObjectHeader bytes + crc(trailer bytes)（尾置，兼作 commit marker）
//
// 两种输入模式（L3，§8.1）：
//   1. 内存模式：DecompressingStreamBuf(data, size)——内部包
//      MemoryChunkSource，构造时尾部解析 trailer。
//   2. 流式模式：DecompressingStreamBuf(ChunkSource, block_area_len)——
//      网络分片流（接收线程 + 有界队列）驱动，块流边界由 META 提供。
//
// 流插件化（2026-08-31）：块拉取/验证/解压委托 fly::ReadPipeline
// （CrcVerifyStage + DecompressStage），本类只做 streambuf 适配。
// 拉取以 block_area_len 为上界（trailer 字节不进管线）；块中截断 =
// failed（零容忍）；块级 raw 直通（comp == unc）由 DecompressStage 处理。
//
// 校验失败（trailer 解析失败 / 块 CRC 失配 / 块流越界或未恰好耗尽 / 解压失败 /
// 源侧 failed）置 checksum_failed_ —— 读完后调用方必须检查 checksum_failed()
// 并按零容忍语义处理（一次重取 → 仍败 FATAL），不得当作正常 EOF 消费。
//
// The source (and for memory mode, its data pointer) must outlive this object.
// 空输入（data == nullptr 或 size == 0）合法：空流，不标记校验失败。
class DecompressingStreamBuf : public std::streambuf {
public:
    // 内存模式：record 全量（块流 + trailer）。
    DecompressingStreamBuf(const char* data, size_t size);
    // 流式模式：源 + 块流区域长度（META 提供）。源必须已就绪元数据。
    // shared 持有（NetworkChunkSource 由多处以 shared 传播）。
    DecompressingStreamBuf(CMSharedPtr<fly::ChunkSource> source, uint64_t block_area_len);
    ~DecompressingStreamBuf() override;

    const CMString& py_name() const { return source_->py_name(); }
    uint64_t total_uncompressed() const { return source_->total_uncompressed(); }
    uint32_t chunk_count() const { return source_->chunk_count(); }

    // 任一校验失败（trailer/块 CRC/结构越界/解压错误/源侧失败）为 true。
    // 读过程与读完后均可查询；失败后流进入 EOF（不再产数据）。
    bool checksum_failed() const { return checksum_failed_; }

protected:
    int_type underflow() override;
    std::streamsize xsgetn(char* s, std::streamsize n) override;

private:
    bool refill();
    void build_pipeline(CompressionType comp);

    CMSharedPtr<fly::ChunkSource> source_;
    uint64_t block_area_len_ = 0;   // 块流区域边界（恰耗校验锚点）
    uint64_t pull_consumed_ = 0;    // 块流区域已消费字节（拉取闭包计数）
    bool checksum_failed_ = false;

    fly::CMUniquePtr<fly::ReadPipeline> pipeline_;
    std::string_view plain_;        // 当前明文块（pipeline scratch，下次 refill 前有效）
    size_t plain_pos_ = 0;
    size_t plain_avail_ = 0;
};
