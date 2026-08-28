#include <storage/cpp/memory_chunk_source.h>
#include <serialization/cpp/object_header.h>
#include <algorithm>
#include <cstring>

namespace fly {

MemoryChunkSource::MemoryChunkSource(const char* data, size_t size)
    : data_(data), size_(size) {
    parse_trailer(data, size);
}

void MemoryChunkSource::parse_trailer(const char* data, size_t size) {
    if (!data || size == 0) return;  // 空输入 = 合法空流

    // 尾部解析 trailer（§4.4）：失败 = 结构损坏（含旧格式前置 header）。
    ObjectHeader header;
    size_t tl = 0;
    if (!ObjectHeader::deserialize_trailer({data, size}, header, tl)) {
        failed_ = true;
        return;
    }
    py_name_ = header.py_name_;
    total_uncompressed_ = header.total_size_;
    chunk_count_ = header.chunk_count_;
    compression_type_ = static_cast<int>(header.compression_type_);
    block_area_len_ = size - tl;
}

int64_t MemoryChunkSource::pull(char* dst, size_t n) {
    if (failed_) return -1;
    uint64_t remain = block_area_len_ > pos_ ? block_area_len_ - pos_ : 0;
    size_t take = static_cast<size_t>(std::min<uint64_t>(n, remain));
    if (take == 0) return 0;  // 块流区域拉尽
    std::memcpy(dst, data_ + pos_, take);
    pos_ += take;
    return static_cast<int64_t>(take);
}

}  // namespace fly
