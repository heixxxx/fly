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
        failure_detail_ = "integrity: trailer parse failed";
        failed_ = true;
        return;
    }
    py_name_ = header.py_name_;
    total_uncompressed_ = header.total_size_;
    chunk_count_ = header.chunk_count_;
    compression_type_ = static_cast<int>(header.compression_type_);
    block_area_len_ = size - tl;

    // 块表对账（B'，§14.1）：逐块头↔表项对照——顺序扫块头（16B 跳跃，
    // 不解压，开销可忽略），每块 comp_len 必须等于表项；走完恰抵块区末尾。
    // 块头域（unc/comp 长度）不在块 CRC 覆盖内——篡改块头导致的边界漂移
    // 由本对账确定性捕获（Σ 一致性是逐项对照的推论，已蕴含）。
    if (header.block_comp_lens_.size() != header.chunk_count_) {
        failure_detail_ = "integrity: block table size mismatch";
        failed_ = true;
        return;
    }
    {
        size_t pos = 0;
        for (uint32_t i = 0; i < header.chunk_count_; ++i) {
            if (pos + 16 > block_area_len_) {
                failure_detail_ = "integrity: block header out of range";
                failed_ = true;
                return;
            }
            int32_t comp;
            std::memcpy(&comp, data_ + pos + 4, sizeof(int32_t));
            if (comp < 0 || static_cast<uint32_t>(comp) != header.block_comp_lens_[i]) {
                failure_detail_ = "integrity: block table reconciliation mismatch";
                failed_ = true;
                return;
            }
            pos += 16 + static_cast<size_t>(comp);
        }
        if (pos != block_area_len_) {
            failure_detail_ = "integrity: block area length mismatch";
            failed_ = true;
            return;
        }
    }
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
