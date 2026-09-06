#include <gtest/gtest.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <container/cpp/container_aliases.h>

struct TestMessage {
    int32_t id = 0;
    CMString name;
    double value = 0.0;
    FLY_SERIALIZE(id, name, value)
};

TEST(SerializationTest, EncodeDecodeMessage) {
    TestMessage original{42, "test", 3.14};
    CMString serialized; FLY_ENCODE(original, serialized);
    TestMessage decoded; FLY_DECODE(serialized, TestMessage, decoded);
    EXPECT_EQ(decoded.id, original.id);
    EXPECT_EQ(decoded.name, original.name);
    EXPECT_DOUBLE_EQ(decoded.value, original.value);
}

TEST(SerializationTest, EmptyMessage) {
    TestMessage original{0, "", 0.0};
    CMString serialized; FLY_ENCODE(original, serialized);
    TestMessage decoded; FLY_DECODE(serialized, TestMessage, decoded);
    EXPECT_EQ(decoded.id, 0); EXPECT_EQ(decoded.name, "");
    EXPECT_DOUBLE_EQ(decoded.value, 0.0);
}

TEST(SerializationTest, LargeString) {
    TestMessage original{1, CMString(1000, 'x'), 2.5};
    CMString serialized; FLY_ENCODE(original, serialized);
    TestMessage decoded; FLY_DECODE(serialized, TestMessage, decoded);
    EXPECT_EQ(decoded.name.size(), 1000);
}

struct VectorMessage {
    CMVector<int32_t> numbers;
    CMVector<CMString> strings;
    FLY_SERIALIZE(numbers, strings)
};

TEST(SerializationTest, VectorOfInts) {
    VectorMessage original; original.numbers = {1, 2, 3, 4, 5};
    CMString serialized; FLY_ENCODE(original, serialized);
    VectorMessage decoded; FLY_DECODE(serialized, VectorMessage, decoded);
    EXPECT_EQ(decoded.numbers.size(), 5);
}

struct NestedInner {
    int32_t x = 0; CMString label;
    FLY_SERIALIZE(x, label)
};

struct NestedOuter {
    NestedInner inner;
    int32_t outer_value = 0;
    FLY_SERIALIZE(inner, outer_value)
};

TEST(SerializationTest, NestedStruct) {
    NestedOuter original;
    original.inner.x = 42; original.inner.label = "nested";
    original.outer_value = 100;
    CMString serialized; FLY_ENCODE(original, serialized);
    NestedOuter decoded; FLY_DECODE(serialized, NestedOuter, decoded);
    EXPECT_EQ(decoded.inner.x, 42);
    EXPECT_EQ(decoded.inner.label, "nested");
    EXPECT_EQ(decoded.outer_value, 100);
}

struct MapMessage {
    CMMap<CMString, int32_t> int_map;
    CMMap<int32_t, CMString> reverse_map;
    FLY_SERIALIZE(int_map, reverse_map)
};

TEST(SerializationTest, Map) {
    MapMessage original;
    original.int_map["k"] = 1; original.reverse_map[1] = "v";
    CMString serialized; FLY_ENCODE(original, serialized);
    MapMessage decoded; FLY_DECODE(serialized, MapMessage, decoded);
    EXPECT_EQ(decoded.int_map.size(), 1);
    EXPECT_EQ(decoded.reverse_map.size(), 1);
}

struct AllTypesMessage {
    int32_t int_val = 0; int64_t long_val = 0;
    CMString str_val; CMVector<int32_t> vec_val;
    FLY_SERIALIZE(int_val, long_val, str_val, vec_val)
};

TEST(SerializationTest, AllTypes) {
    AllTypesMessage original{123, 9876543210LL, "test", {1, 2, 3}};
    CMString serialized; FLY_ENCODE(original, serialized);
    AllTypesMessage decoded; FLY_DECODE(serialized, AllTypesMessage, decoded);
    EXPECT_EQ(decoded.int_val, 123);
    EXPECT_EQ(decoded.vec_val.size(), 3);
}

TEST(SerializationTest, LargeData) {
    VectorMessage original;
    original.numbers = CMVector<int32_t>(10000, 42);
    CMString serialized; FLY_ENCODE(original, serialized);
    EXPECT_GT(serialized.size(), 10000);
    VectorMessage decoded; FLY_DECODE(serialized, VectorMessage, decoded);
    EXPECT_EQ(decoded.numbers.size(), 10000);
}

TEST(SerializationTest, ZeroValues) {
    AllTypesMessage original{};
    CMString serialized; FLY_ENCODE(original, serialized);
    AllTypesMessage decoded; FLY_DECODE(serialized, AllTypesMessage, decoded);
    EXPECT_EQ(decoded.int_val, 0);
    EXPECT_TRUE(decoded.str_val.empty());
    EXPECT_TRUE(decoded.vec_val.empty());
}