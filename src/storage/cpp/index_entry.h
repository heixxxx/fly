#pragma once

#include <container/cpp/container_aliases.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <cstdint>

struct IndexEntry {
    CMString object_name_;
    CMString file_name_;
    int64_t offset_ = 0;
    int64_t size_ = 0;
    bool is_large_ = false;
    int32_t block_count_ = 0;
    CMString host_;
    CMString write_context_hash_;

    FLY_SERIALIZE(object_name_, file_name_, offset_, size_, is_large_, block_count_, host_, write_context_hash_);
};