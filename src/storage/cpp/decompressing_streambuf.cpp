#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/data_checksum.h>
#include <cstring>
#include <string_view>

DecompressingStreamBuf::DecompressingStreamBuf(const char* data, size_t size)
    : chunk_data_(nullptr), chunk_data_size_(0) {
    if (!data || size == 0) return;  // 空输入 = 合法空流

    // 尾部解析 trailer（§4.4）：失败 = 结构损坏（含旧格式前置 header 的 record）
    // → checksum_failed_，调用方按零容忍语义处理。不做静默空流降级。
    ObjectHeader header;
    size_t trailer_len = 0;
    if (!ObjectHeader::deserialize_trailer({data, size}, header, trailer_len)) {
        checksum_failed_ = true;
        return;
    }

    py_name_ = header.py_name_;
    total_uncompressed_ = header.total_size_;
    chunk_count_ = header.chunk_count_;
    auto comp_type = static_cast<CompressionType>(header.compression_type_);
    if (comp_type != CompressionType::NONE) {
        compressor_ = CompressorFactory::create(comp_type);
    }

    // 块流必须恰好消耗 chunk_data_size_（refill 越界即结构损坏）。
    chunk_data_ = data;
    chunk_data_size_ = size - trailer_len;
    chunk_data_pos_ = 0;
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

bool DecompressingStreamBuf::refill() {
    if (checksum_failed_) return false;
    if (chunk_data_pos_ >= chunk_data_size_) return false;

    // 块头 16B：[i32 unc][i32 comp][u64 crc]（§4.4）。任何越界 = 结构损坏。
    constexpr size_t chunk_header_sz = sizeof(int32_t) * 2 + sizeof(uint64_t);
    if (chunk_data_pos_ + chunk_header_sz > chunk_data_size_) {
        checksum_failed_ = true;
        return false;
    }

    int32_t uncomp_size, comp_size;
    uint64_t stored_crc;
    std::memcpy(&uncomp_size, chunk_data_ + chunk_data_pos_, sizeof(int32_t));
    chunk_data_pos_ += sizeof(int32_t);
    std::memcpy(&comp_size, chunk_data_ + chunk_data_pos_, sizeof(int32_t));
    chunk_data_pos_ += sizeof(int32_t);
    std::memcpy(&stored_crc, chunk_data_ + chunk_data_pos_, sizeof(uint64_t));
    chunk_data_pos_ += sizeof(uint64_t);

    if (comp_size < 0 || uncomp_size < 0 ||
        chunk_data_pos_ + static_cast<size_t>(comp_size) > chunk_data_size_) {
        checksum_failed_ = true;
        return false;
    }

    // 写入时刻锚点 CRC：磁盘 → server → 网络 → client → 解压 全生命周期覆盖。
    if (fly::data_checksum(chunk_data_ + chunk_data_pos_, static_cast<size_t>(comp_size)) !=
        stored_crc) {
        checksum_failed_ = true;
        return false;
    }

    std::string_view comp_view(chunk_data_ + chunk_data_pos_, static_cast<size_t>(comp_size));
    chunk_data_pos_ += static_cast<size_t>(comp_size);

    if (compressor_) {
        // Zero-copy: decompress directly into buffer_
        buffer_.resize(static_cast<size_t>(uncomp_size));
        int32_t written = compressor_->decompress_to(comp_view, buffer_.data(), buffer_.size());
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
        buffer_.resize(comp_view.size());
        std::memcpy(buffer_.data(), comp_view.data(), comp_view.size());
    }

    buffer_pos_ = 0;
    buffer_avail_ = buffer_.size();
    return !buffer_.empty();
}
