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
    const int comp_raw = mem->compression_type();
    source_ = std::move(mem);
    build_pipeline(static_cast<CompressionType>(comp_raw));
}

DecompressingStreamBuf::DecompressingStreamBuf(CMSharedPtr<fly::ChunkSource> source,
                                               uint64_t block_area_len)
    : source_(std::move(source)), block_area_len_(block_area_len) {
    // 流式模式（L3 §8.1）：META 提供块流边界与元数据（server 发送前 pread
    // 尾部解析）。trailer 完整性由 trailer 自身 CRC 承担（不进管线）。
    checksum_failed_ = source_->failed();
    build_pipeline(static_cast<CompressionType>(source_->compression_type()));
}

void DecompressingStreamBuf::build_pipeline(CompressionType comp) {
    // 拉取以块流区域为上界（trailer 字节不进管线）；pull_consumed_ 供恰耗判定。
    const uint64_t area = block_area_len_;
    auto src = source_;
    auto pull = [this, src, area](char* dst, size_t n) -> int64_t {
        if (pull_consumed_ >= area) return 0;
        if (n > static_cast<size_t>(area - pull_consumed_)) {
            n = static_cast<size_t>(area - pull_consumed_);
        }
        const int64_t r = src->pull(dst, n);
        if (r > 0) pull_consumed_ += static_cast<uint64_t>(r);
        return r;
    };
    pipeline_ = std::make_unique<fly::ReadPipeline>(fly::make_block_read_pipeline(comp, std::move(pull)));
}

DecompressingStreamBuf::~DecompressingStreamBuf() = default;

DecompressingStreamBuf::int_type DecompressingStreamBuf::underflow() {
    if (plain_pos_ < plain_avail_) {
        return traits_type::to_int_type(static_cast<unsigned char>(plain_[plain_pos_]));
    }
    if (!refill()) {
        return traits_type::eof();
    }
    return traits_type::to_int_type(static_cast<unsigned char>(plain_[plain_pos_]));
}

std::streamsize DecompressingStreamBuf::xsgetn(char* s, std::streamsize n) {
    std::streamsize copied = 0;
    while (copied < n) {
        if (plain_pos_ >= plain_avail_) {
            if (!refill()) break;
        }
        auto avail = std::min(
            static_cast<std::streamsize>(plain_avail_ - plain_pos_),
            n - copied);
        std::memcpy(s + copied, plain_.data() + plain_pos_, static_cast<size_t>(avail));
        plain_pos_ += static_cast<size_t>(avail);
        copied += avail;
    }
    return copied;
}

bool DecompressingStreamBuf::pull_exact(char* dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int64_t r = source_->pull(dst + got, n - got);
        if (r <= 0) return false;  // 源侧失败 / EOF（不足 n = 截断）
        got += static_cast<size_t>(r);
    }
    return true;
}

bool DecompressingStreamBuf::refill() {
    if (checksum_failed_) return false;
    if (pull_consumed_ >= block_area_len_) return false;  // 块流恰耗完成

    // 块拉取 + CRC 验证 + 解压委托读管线；失败（CRC 失配/截断/解压错误）
    // 即零容忍信号。
    fly::BlockData b;
    if (!pipeline_->next_block(b)) {
        if (pipeline_->failed()) checksum_failed_ = true;
        return false;
    }
    plain_ = b.plain;
    plain_pos_ = 0;
    plain_avail_ = b.plain.size();
    return plain_avail_ > 0;
}
