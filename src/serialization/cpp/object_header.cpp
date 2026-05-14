#include <serialization/cpp/object_header.h>
#include <cstring>
#include <stdexcept>

CMString ObjectHeader::serialize() const {
    CMString result;
    int64_t total = fixed_header_size() + static_cast<int64_t>(py_name.size());
    result.resize(static_cast<size_t>(total));

    int64_t offset = 0;
    std::memcpy(result.data() + offset, &magic, sizeof(magic));
    offset += sizeof(magic);

    std::memcpy(result.data() + offset, &version, sizeof(version));
    offset += sizeof(version);

    uint16_t name_len = static_cast<uint16_t>(py_name.size());
    std::memcpy(result.data() + offset, &name_len, sizeof(name_len));
    offset += sizeof(name_len);

    std::memcpy(result.data() + offset, &total_size, sizeof(total_size));
    offset += sizeof(total_size);

    std::memcpy(result.data() + offset, &chunk_count, sizeof(chunk_count));
    offset += sizeof(chunk_count);

    std::memcpy(result.data() + offset, &compression_type, sizeof(compression_type));
    offset += sizeof(compression_type);

    if (!py_name.empty()) {
        std::memcpy(result.data() + offset, py_name.data(), py_name.size());
    }

    return result;
}

ObjectHeader ObjectHeader::deserialize(const CMString& data, int64_t& offset) {
    ObjectHeader header;

    if (static_cast<int64_t>(data.size()) < offset + fixed_header_size()) {
        throw std::runtime_error("ObjectHeader: insufficient data for fixed header");
    }

    std::memcpy(&header.magic, data.data() + offset, sizeof(header.magic));
    offset += sizeof(header.magic);

    if (!header.is_valid()) {
        throw std::runtime_error("ObjectHeader: invalid magic number");
    }

    std::memcpy(&header.version, data.data() + offset, sizeof(header.version));
    offset += sizeof(header.version);

    if (header.version > FLY_OBJECT_VERSION) {
        throw std::runtime_error("ObjectHeader: unsupported version " + std::to_string(header.version));
    }

    std::memcpy(&header.py_name_len, data.data() + offset, sizeof(header.py_name_len));
    offset += sizeof(header.py_name_len);

    std::memcpy(&header.total_size, data.data() + offset, sizeof(header.total_size));
    offset += sizeof(header.total_size);

    std::memcpy(&header.chunk_count, data.data() + offset, sizeof(header.chunk_count));
    offset += sizeof(header.chunk_count);

    std::memcpy(&header.compression_type, data.data() + offset, sizeof(header.compression_type));
    offset += sizeof(header.compression_type);

    if (header.py_name_len > 0) {
        if (static_cast<int64_t>(data.size()) < offset + header.py_name_len) {
            throw std::runtime_error("ObjectHeader: insufficient data for py_name");
        }
        header.py_name.assign(data.data() + offset, header.py_name_len);
        offset += header.py_name_len;
    }

    return header;
}

bool ObjectHeader::is_valid() const {
    return magic == FLY_OBJECT_MAGIC;
}