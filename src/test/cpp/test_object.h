#pragma once

#include <container/cpp/container_aliases.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <cstdint>

class TestObject {
public:
    int64_t value = 0;
    CMString name;

    TestObject() = default;
    TestObject(int64_t v, const CMString& n = "") : value(v), name(n) {}

    FLY_SERIALIZE(value, name);
};
