#include <serialization/cpp/object_header.h>
#include <cstring>
#include <stdexcept>

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

ObjectHeader ObjectHeader::deserialize(const CMString& data, int64_t& offset) {
    ObjectHeader header;

    if (static_cast<int64_t>(data.size()) < offset + fixed_header_size()) {
        throw std::runtime_error("ObjectHeader: insufficient data for fixed header");
    }

    std::memcpy(&header.magic_, data.data() + offset, sizeof(header.magic_));
    offset += sizeof(header.magic_);

    if (!header.is_valid()) {
        throw std::runtime_error("ObjectHeader: invalid magic number");
    }

    std::memcpy(&header.version_, data.data() + offset, sizeof(header.version_));
    offset += sizeof(header.version_);

    if (header.version_ > FLY_OBJECT_VERSION) {
        throw std::runtime_error("ObjectHeader: unsupported version " + std::to_string(header.version_));
    }

    std::memcpy(&header.py_name_len_, data.data() + offset, sizeof(header.py_name_len_));
    offset += sizeof(header.py_name_len_);

    std::memcpy(&header.total_size_, data.data() + offset, sizeof(header.total_size_));
    offset += sizeof(header.total_size_);

    std::memcpy(&header.chunk_count_, data.data() + offset, sizeof(header.chunk_count_));
    offset += sizeof(header.chunk_count_);

    std::memcpy(&header.compression_type_, data.data() + offset, sizeof(header.compression_type_));
    offset += sizeof(header.compression_type_);

    if (header.py_name_len_ > 0) {
        if (static_cast<int64_t>(data.size()) < offset + header.py_name_len_) {
            throw std::runtime_error("ObjectHeader: insufficient data for py_name");
        }
        header.py_name_.assign(data.data() + offset, header.py_name_len_);
        offset += header.py_name_len_;
    }

    return header;
}

bool ObjectHeader::is_valid() const {
    return magic_ == FLY_OBJECT_MAGIC;
}
