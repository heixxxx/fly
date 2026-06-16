#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <string_view>

constexpr uint32_t FLY_OBJECT_MAGIC = 0x464C5900;
constexpr uint8_t FLY_OBJECT_VERSION = 1;

struct ObjectHeader {
    uint32_t magic_ = FLY_OBJECT_MAGIC;
    uint8_t version_ = FLY_OBJECT_VERSION;
    uint16_t py_name_len_ = 0;
    CMString py_name_;
    uint64_t total_size_ = 0;
    uint32_t chunk_count_ = 0;
    uint8_t compression_type_ = 0;

    CMString serialize() const;
    // Zero-copy: accepts std::string_view — CMString, FlyBuffer data, or raw
    // pointer+size all construct a string_view implicitly without copying.
    static ObjectHeader deserialize(std::string_view data, int64_t& offset);

    static constexpr int64_t fixed_header_size() {
        return sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) +
               sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint8_t);
    }

    bool is_valid() const;
};