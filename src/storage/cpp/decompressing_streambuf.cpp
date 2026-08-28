#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/memory_chunk_source.h>
#include <common/cpp/data_checksum.h>
#include <cstring>
#include <string_view>

DecompressingStreamBuf::DecompressingStreamBuf(const char* data, size_t size) {
    // 内存模式：MemoryChunkSource 构造时尾部解析 trailer（失败 = failed_）。
    auto mem = CMMakeShared<fly::MemoryChunkSource>(data, size);
    checksum_failed_ = mem->failed();
    block_area_len_ = mem->block_area_len();
    int comp_raw = mem->compression_type();
    if (!checksum_failed_ && comp_raw != static_cast<int>(CompressionType::NONE) &&
        comp_raw >= 0) {
        compressor_ = CompressorFactory::create(static_cast<CompressionType>(comp_raw));
    }
    source_ = std::move(mem);
    buffer_.reserve(4096);
}

DecompressingStreamBuf::DecompressingStreamBuf(CMSharedPtr<fly::ChunkSource> source,
                                               uint64_t block_area_len)
    : source_(std::move(source)), block_area_len_(block_area_len) {
    // 流式模式（L3 §8.1）：META 提供块流边界与元数据（server 发送前 pread
    // 尾部解析）。trailer 完整性由 DIGEST 根覆盖（接收线程验证，failed() 呈现）。
    checksum_failed_ = source_->failed();
    int comp_raw = source_->compression_type();
    if (!checksum_failed_ && comp_raw != static_cast<int>(CompressionType::NONE) &&
        comp_raw >= 0) {
        compressor_ = CompressorFactory::create(static_cast<CompressionType>(comp_raw));
    }
    buffer_.reserve(4096);
}

DecompressingStreamBuf::~DecompressingStreamBuf() = default;

DecompressingStreamBuf::int_type DecompressingStreamBuf::underflow() {
    if (buffer_pos_ < buffer_avail_) {
        return traits_type::to_int_type(static_cast<unsigned char>(buffer_[buffer_pos_]));
    }
    if (!refill()) {
        return traits_type::eof();
    }
    return traits_type::to_int_type(static_cast<unsigned char>(buffer_[buffer_pos_]));
}

std::streamsize DecompressingStreamBuf::xsgetn(char* s, std::streamsize n) {
    std::streamsize copied = 0;
    while (copied < n) {
        if (buffer_pos_ >= buffer_avail_) {
            if (!refill()) break;
        }
        auto avail = std::min(
            static_cast<std::streamsize>(buffer_avail_ - buffer_pos_),
            n - copied);
        std::memcpy(s + copied, buffer_.data() + buffer_pos_, static_cast<size_t>(avail));
        buffer_pos_ += static_cast<size_t>(avail);
        copied += avail;
    }
    return copied;
}

bool DecompressingStreamBuf::pull_exact(char* dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        int64_t r = source_->pull(dst + got, n - got);
        if (r < 0) return false;  // 源侧失败
        if (r == 0) return false;  // EOF（不足 n = 截断）
        got += static_cast<size_t>(r);
    }
    return true;
}

bool DecompressingStreamBuf::refill() {
    if (checksum_failed_) return false;
    if (consumed_ >= block_area_len_) return false;  // 块流恰耗完成

    // 块头 16B：[i32 unc][i32 comp][u64 crc]（§4.4）。任何越界/截断 = 结构损坏。
    constexpr size_t chunk_header_sz = sizeof(int32_t) * 2 + sizeof(uint64_t);
    char hdr[chunk_header_sz];
    if (!pull_exact(hdr, chunk_header_sz)) {
        checksum_failed_ = true;  // 截断（块流区域未恰耗）或源失败
        return false;
    }
    consumed_ += chunk_header_sz;

    int32_t uncomp_size, comp_size;
    uint64_t stored_crc;
    std::memcpy(&uncomp_size, hdr, sizeof(int32_t));
    std::memcpy(&comp_size, hdr + sizeof(int32_t), sizeof(int32_t));
    std::memcpy(&stored_crc, hdr + sizeof(int32_t) * 2, sizeof(uint64_t));

    if (comp_size < 0 || uncomp_size < 0 ||
        consumed_ + static_cast<uint64_t>(comp_size) > block_area_len_) {
        checksum_failed_ = true;
        return false;
    }

    // 块数据拉取（流式下可能跨多次 pull）。
    CMVector<char> comp_buf(static_cast<size_t>(comp_size));
    if (!pull_exact(comp_buf.data(), static_cast<size_t>(comp_size))) {
        checksum_failed_ = true;
        return false;
    }
    consumed_ += static_cast<uint64_t>(comp_size);

    // 写入时刻锚点 CRC：磁盘 → server → 网络 → client → 解压 全生命周期覆盖。
    if (fly::data_checksum(comp_buf.data(), static_cast<size_t>(comp_size)) != stored_crc) {
        checksum_failed_ = true;
        return false;
    }

    if (compressor_) {
        // Zero-copy: decompress directly into buffer_
        buffer_.resize(static_cast<size_t>(uncomp_size));
        int32_t written = compressor_->decompress_to(
            {comp_buf.data(), static_cast<size_t>(comp_size)},
            buffer_.data(), buffer_.size());
        if (written < 0 || written != uncomp_size) {
            // CRC 已过验但解压失败 = 实现层缺陷或 CRC 漏过的损坏——按数据
            // 损坏上报（零容忍），不静默截断。
            buffer_.clear();
            checksum_failed_ = true;
            return false;
        }
        buffer_.resize(static_cast<size_t>(written));
    } else {
        // No compression: direct copy
        buffer_.swap(comp_buf);
        buffer_.resize(static_cast<size_t>(comp_size));
    }

    buffer_pos_ = 0;
    buffer_avail_ = buffer_.size();
    return !buffer_.empty();
}
