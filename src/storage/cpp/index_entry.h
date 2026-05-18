#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

struct IndexEntry {
    CMString object_name;
    CMString file_name;
    int64_t offset = 0;
    int64_t size = 0;
    bool is_large = false;
    int32_t block_count = 0;
    int8_t compression_type = 0;
    CMString host;

    FLY_SERIALIZE_BEGIN(3)
        FLY_FIELD(object_name);
        FLY_FIELD(file_name);
        FLY_FIELD(offset);
        FLY_FIELD(size);
        FLY_BOOL(is_large);
        FLY_FIELD(block_count);
        if (version >= 2) {
            FLY_FIELD(compression_type);
        }
        if (version >= 3) {
            FLY_FIELD(host);
        }
    FLY_SERIALIZE_END
};