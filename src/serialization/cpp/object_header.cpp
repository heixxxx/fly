#include <serialization/cpp/object_header.h>
#include <common/cpp/data_checksum.h>
#include <cstring>
#include <utility>

CMString ObjectHeader::serialize() const {
    CMString result;
    int64_t total = fixed_header_size() + static_cast<int64_t>(py_name_.size());
    result.resize(static_cast<size_t>(total));

    int64_t offset = 0;
    std::memcpy(result.data() + offset, &magic_, sizeof(magic_));
    offset += sizeof(magic_);

    std::memcpy(result.data() + offset, &version_, sizeof(version_));
    offset += sizeof(version_);

    uint16_t name_len = static_cast<uint16_t>(py_name_.size());
    std::memcpy(result.data() + offset, &name_len, sizeof(name_len));
    offset += sizeof(name_len);

    // v2：内存路径块表恒空（block_comp_lens_ 是磁盘语义，不进内存序列化），
    // block_table_len 占位 0，保持 fixed 段字段与 trailer 布局对齐。
    const uint32_t mem_table_len = 0;
    std::memcpy(result.data() + offset, &mem_table_len, sizeof(mem_table_len));
    offset += sizeof(mem_table_len);

    std::memcpy(result.data() + offset, &total_size_, sizeof(total_size_));
    offset += sizeof(total_size_);

    std::memcpy(result.data() + offset, &chunk_count_, sizeof(chunk_count_));
    offset += sizeof(chunk_count_);

    std::memcpy(result.data() + offset, &compression_type_, sizeof(compression_type_));
    offset += sizeof(compression_type_);

    if (!py_name_.empty()) {
        std::memcpy(result.data() + offset, py_name_.data(), py_name_.size());
    }

    return result;
}

bool ObjectHeader::deserialize(std::string_view data, int64_t& offset, ObjectHeader& out) {
    ObjectHeader header;

    if (static_cast<int64_t>(data.size()) < offset + fixed_header_size()) {
        return false;
    }

    std::memcpy(&header.magic_, data.data() + offset, sizeof(header.magic_));
    offset += sizeof(header.magic_);

    if (!header.is_valid()) {
        return false;
    }

    std::memcpy(&header.version_, data.data() + offset, sizeof(header.version_));
    offset += sizeof(header.version_);

    if (header.version_ > FLY_OBJECT_VERSION) {
        return false;
    }

    std::memcpy(&header.py_name_len_, data.data() + offset, sizeof(header.py_name_len_));
    offset += sizeof(header.py_name_len_);

    // v2：跳过 block_table_len 占位（内存路径恒 0；磁盘语义字段）。
    uint32_t mem_table_len = 0;
    std::memcpy(&mem_table_len, data.data() + offset, sizeof(mem_table_len));
    offset += sizeof(mem_table_len);
    if (mem_table_len != 0) {
        return false;  // 内存格式块表必须为空
    }

    std::memcpy(&header.total_size_, data.data() + offset, sizeof(header.total_size_));
    offset += sizeof(header.total_size_);

    std::memcpy(&header.chunk_count_, data.data() + offset, sizeof(header.chunk_count_));
    offset += sizeof(header.chunk_count_);

    std::memcpy(&header.compression_type_, data.data() + offset, sizeof(header.compression_type_));
    offset += sizeof(header.compression_type_);

    if (header.py_name_len_ > 0) {
        if (static_cast<int64_t>(data.size()) < offset + header.py_name_len_) {
            return false;
        }
        header.py_name_.assign(data.data() + offset, header.py_name_len_);
        offset += header.py_name_len_;
    }

    out = std::move(header);
    return true;
}

bool ObjectHeader::is_valid() const {
    return magic_ == FLY_OBJECT_MAGIC;
}

CMString ObjectHeader::serialize_trailer() const {
    const size_t fixed = static_cast<size_t>(fixed_header_size());
    const uint32_t table_len =
        static_cast<uint32_t>(block_comp_lens_.size() * sizeof(uint32_t));
    const size_t body = table_len + py_name_.size() + fixed;
    CMString result;
    result.resize(body + sizeof(uint64_t));
    char* p = result.data();

    if (table_len > 0) {
        std::memcpy(p, block_comp_lens_.data(), table_len);
        p += table_len;
    }
    std::memcpy(p, py_name_.data(), py_name_.size());
    p += py_name_.size();

    std::memcpy(p, &magic_, sizeof(magic_));
    p += sizeof(magic_);
    std::memcpy(p, &version_, sizeof(version_));
    p += sizeof(version_);
    uint16_t name_len = static_cast<uint16_t>(py_name_.size());
    std::memcpy(p, &name_len, sizeof(name_len));
    p += sizeof(name_len);
    std::memcpy(p, &table_len, sizeof(table_len));
    p += sizeof(table_len);
    std::memcpy(p, &total_size_, sizeof(total_size_));
    p += sizeof(total_size_);
    std::memcpy(p, &chunk_count_, sizeof(chunk_count_));
    p += sizeof(chunk_count_);
    std::memcpy(p, &compression_type_, sizeof(compression_type_));
    p += sizeof(compression_type_);

    uint64_t crc = fly::data_checksum(result.data(), body);
    std::memcpy(p, &crc, sizeof(crc));
    return result;
}

bool ObjectHeader::deserialize_trailer(std::string_view record, ObjectHeader& out,
                                       size_t& trailer_len) {
    ObjectHeader header;

    const size_t fixed = static_cast<size_t>(fixed_header_size());
    const size_t crc_sz = sizeof(uint64_t);
    if (record.size() < fixed + crc_sz) return false;

    // 末 8B：trailer CRC（覆盖 块表+py_name+fixed 全部 header 字节）。
    const char* crc_p = record.data() + record.size() - crc_sz;
    uint64_t stored_crc;
    std::memcpy(&stored_crc, crc_p, crc_sz);

    // 其前 fixed 24B：定长锚定（v2 布局，§14.1）。
    const char* hp = crc_p - fixed;
    std::memcpy(&header.magic_, hp, sizeof(header.magic_));
    if (!header.is_valid()) return false;
    hp += sizeof(header.magic_);

    std::memcpy(&header.version_, hp, sizeof(header.version_));
    if (header.version_ > FLY_OBJECT_VERSION) return false;
    hp += sizeof(header.version_);

    std::memcpy(&header.py_name_len_, hp, sizeof(header.py_name_len_));
    hp += sizeof(header.py_name_len_);

    uint32_t table_len = 0;
    std::memcpy(&table_len, hp, sizeof(table_len));
    hp += sizeof(table_len);

    std::memcpy(&header.total_size_, hp, sizeof(header.total_size_));
    hp += sizeof(header.total_size_);

    std::memcpy(&header.chunk_count_, hp, sizeof(header.chunk_count_));
    hp += sizeof(header.chunk_count_);

    std::memcpy(&header.compression_type_, hp, sizeof(header.compression_type_));

    // 双口径互验：表长 == 块数 × 4（防 chunk_count/table_len 域损坏——
    // 任一域翻转使两口径失配，确定性拒绝）。
    if (table_len != header.chunk_count_ * sizeof(uint32_t)) return false;

    size_t body_len = table_len + header.py_name_len_ + fixed;
    if (record.size() < body_len + crc_sz) return false;

    // CRC 覆盖 [块表起点, fixed 结束) 的连续 header 段。
    const char* body_start = crc_p - body_len;
    if (fly::data_checksum(body_start, body_len) != stored_crc) return false;

    if (table_len > 0) {
        header.block_comp_lens_.resize(table_len / sizeof(uint32_t));
        std::memcpy(header.block_comp_lens_.data(), body_start, table_len);
    }
    if (header.py_name_len_ > 0) {
        header.py_name_.assign(body_start + table_len, header.py_name_len_);
    }

    out = std::move(header);
    trailer_len = body_len + crc_sz;
    return true;
}
