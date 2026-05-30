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
    CMString host;

    FLY_SERIALIZE(object_name, file_name, offset, size, is_large, block_count, host);
};