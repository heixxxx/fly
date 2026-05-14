#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>

constexpr uint32_t FLY_OBJECT_MAGIC = 0x464C5900;
constexpr uint8_t FLY_OBJECT_VERSION = 1;

struct ObjectHeader {
    uint32_t magic = FLY_OBJECT_MAGIC;
    uint8_t version = FLY_OBJECT_VERSION;
    uint16_t py_name_len = 0;
    CMString py_name;
    uint64_t total_size = 0;
    uint32_t chunk_count = 0;
    uint8_t compression_type = 0;

    CMString serialize() const;
    static ObjectHeader deserialize(const CMString& data, int64_t& offset);

    static constexpr int64_t fixed_header_size() {
        return sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) +
               sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint8_t);
    }

    bool is_valid() const;
};