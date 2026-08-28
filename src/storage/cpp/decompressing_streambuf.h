#pragma once

#include <storage/cpp/compressor.h>
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
// 构造从输入【尾部】解析 trailer，块流区域 = size - trailer_len。
// 校验失败（trailer 解析失败 / 块 CRC 失配 / 块流越界或未恰好耗尽 / 解压失败）
// 置 checksum_failed_ —— 读完后调用方必须检查 checksum_failed() 并按零容忍
// 语义处理（一次重取 → 仍败 FATAL），不得当作正常 EOF 消费。
//
// Decompresses chunks on demand, serving decompressed bytes via the
// std::streambuf interface. Designed to be paired with bitsery::InputStreamAdapter
// for zero-copy streaming deserialization:
//
//   DecompressingStreamBuf dsbuf(data, size);
//   std::istream is(&dsbuf);
//   FlyInputStreamAdapter adapter(is);
//   bitsery::quickDeserialization(std::move(adapter), obj);
//
// The input data pointer must outlive this object.
// 空输入（data == nullptr 或 size == 0）合法：空流，不标记校验失败。
class DecompressingStreamBuf : public std::streambuf {
public:
    DecompressingStreamBuf(const char* data, size_t size);
    ~DecompressingStreamBuf() override;

    const CMString& py_name() const { return py_name_; }

    // trailer 元数据（尾部解析所得；空输入/解析失败时为 0）。
    uint64_t total_uncompressed() const { return total_uncompressed_; }
    uint32_t chunk_count() const { return chunk_count_; }

    // 任一校验失败（trailer/块 CRC/结构越界/解压错误）为 true。读过程与读完后
    // 均可查询；失败后流进入 EOF（不再产数据）。
    bool checksum_failed() const { return checksum_failed_; }

protected:
    int_type underflow() override;
    std::streamsize xsgetn(char* s, std::streamsize n) override;

private:
    bool refill();

    const char* chunk_data_;
    size_t chunk_data_size_;
    size_t chunk_data_pos_ = 0;

    CMUniquePtr<Compressor> compressor_;
    CMString py_name_;
    uint64_t total_uncompressed_ = 0;
    uint32_t chunk_count_ = 0;
    bool checksum_failed_ = false;

    CMVector<char> buffer_;
    size_t buffer_pos_ = 0;
    size_t buffer_avail_ = 0;
};
