#pragma once

#include <common/cpp/common_types.h>
#include <serialization/cpp/serialization_macros.h>
#include <cstdint>

class TestObject {
public:
    int64_t value = 0;
    CMString name;

    TestObject() = default;
    TestObject(int64_t v, const CMString& n = "") : value(v), name(n) {}

    FLY_SERIALIZE(value, name);
};
