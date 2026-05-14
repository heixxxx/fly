#include <gtest/gtest.h>
#include <serialization/cpp/serialization_macros.h>
#include <common/cpp/common_types.h>

struct TestMessage {
    int32_t id;
    CMString name;
    double value;
};

TEST(SerializationTest, EncodeDecodeMessage) {
    TestMessage original{42, "test", 3.14};
    
    std::string serialized;
    FLY_ENCODE(original, serialized);
    
    TestMessage decoded;
    FLY_DECODE(serialized, TestMessage, decoded);
    
    EXPECT_EQ(decoded.id, original.id);
    EXPECT_EQ(decoded.name, original.name);
    EXPECT_DOUBLE_EQ(decoded.value, original.value);
}

TEST(SerializationTest, EmptyMessage) {
    TestMessage original{0, "", 0.0};
    
    std::string serialized;
    FLY_ENCODE(original, serialized);
    
    TestMessage decoded;
    FLY_DECODE(serialized, TestMessage, decoded);
    
    EXPECT_EQ(decoded.id, 0);
    EXPECT_EQ(decoded.name, "");
    EXPECT_DOUBLE_EQ(decoded.value, 0.0);
}

TEST(SerializationTest, LargeString) {
    std::string large_str(1000, 'x');
    TestMessage original{1, large_str, 2.5};
    
    std::string serialized;
    FLY_ENCODE(original, serialized);
    
    TestMessage decoded;
    FLY_DECODE(serialized, TestMessage, decoded);
    
    EXPECT_EQ(decoded.name, large_str);
}

TEST(SerializationTest, EncodeDecodeBytes) {
    TestMessage original{100, "bytes_test", 1.5};
    
    CMVector<unsigned char> serialized;
    FLY_ENCODE_TO_BYTES(original, serialized);
    
    TestMessage decoded;
    FLY_DECODE_FROM_BYTES(serialized, TestMessage, decoded);
    
    EXPECT_EQ(decoded.id, original.id);
    EXPECT_EQ(decoded.name, original.name);
}

struct VectorMessage {
    CMVector<int32_t> numbers;
    CMVector<CMString> strings;
};

TEST(SerializationTest, VectorOfInts) {
    VectorMessage original;
    original.numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    original.strings = {};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    VectorMessage decoded;
    FLY_DECODE(serialized, VectorMessage, decoded);
    
    EXPECT_EQ(decoded.numbers.size(), 10);
    EXPECT_EQ(decoded.numbers[5], 6);
}

TEST(SerializationTest, VectorOfStrings) {
    VectorMessage original;
    original.numbers = {};
    original.strings = {"hello", "world", "test"};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    VectorMessage decoded;
    FLY_DECODE(serialized, VectorMessage, decoded);
    
    EXPECT_EQ(decoded.strings.size(), 3);
    EXPECT_EQ(decoded.strings[1], "world");
}

TEST(SerializationTest, MixedVectors) {
    VectorMessage original;
    original.numbers = {100, 200};
    original.strings = {"a", "b", "c"};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    VectorMessage decoded;
    FLY_DECODE(serialized, VectorMessage, decoded);
    
    EXPECT_EQ(decoded.numbers.size(), 2);
    EXPECT_EQ(decoded.strings.size(), 3);
}

struct NestedInner {
    int32_t x;
    CMString label;
};

struct NestedOuter {
    NestedInner inner;
    int32_t outer_value;
};

TEST(SerializationTest, NestedStruct) {
    NestedOuter original;
    original.inner.x = 42;
    original.inner.label = "nested";
    original.outer_value = 100;
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    NestedOuter decoded;
    FLY_DECODE(serialized, NestedOuter, decoded);
    
    EXPECT_EQ(decoded.inner.x, 42);
    EXPECT_EQ(decoded.inner.label, "nested");
    EXPECT_EQ(decoded.outer_value, 100);
}

struct MapMessage {
    CMMap<CMString, int32_t> int_map;
    CMMap<int32_t, CMString> reverse_map;
};

TEST(SerializationTest, MapOfStringToInt) {
    MapMessage original;
    original.int_map["key1"] = 100;
    original.int_map["key2"] = 200;
    original.reverse_map = {};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    MapMessage decoded;
    FLY_DECODE(serialized, MapMessage, decoded);
    
    EXPECT_EQ(decoded.int_map.size(), 2);
    EXPECT_EQ(decoded.int_map["key1"], 100);
}

TEST(SerializationTest, MapOfIntToString) {
    MapMessage original;
    original.int_map = {};
    original.reverse_map[1] = "one";
    original.reverse_map[2] = "two";
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    MapMessage decoded;
    FLY_DECODE(serialized, MapMessage, decoded);
    
    EXPECT_EQ(decoded.reverse_map.size(), 2);
    EXPECT_EQ(decoded.reverse_map[1], "one");
}

struct AllTypesMessage {
    int32_t int_val;
    int64_t long_val;
    double double_val;
    CMString str_val;
    CMVector<int32_t> vec_val;
};

TEST(SerializationTest, AllTypesCombined) {
    AllTypesMessage original;
    original.int_val = 123;
    original.long_val = 9876543210LL;
    original.double_val = 3.14159265358979;
    original.str_val = "complete test";
    original.vec_val = {1, 2, 3};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    AllTypesMessage decoded;
    FLY_DECODE(serialized, AllTypesMessage, decoded);
    
    EXPECT_EQ(decoded.int_val, 123);
    EXPECT_EQ(decoded.long_val, 9876543210LL);
    EXPECT_DOUBLE_EQ(decoded.double_val, 3.14159265358979);
    EXPECT_EQ(decoded.str_val, "complete test");
    EXPECT_EQ(decoded.vec_val.size(), 3);
}

TEST(SerializationTest, LargeData) {
    CMVector<int32_t> large_vec(10000, 42);
    
    VectorMessage original;
    original.numbers = large_vec;
    original.strings = {};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    EXPECT_GT(serialized.size(), 10000);
    
    VectorMessage decoded;
    FLY_DECODE(serialized, VectorMessage, decoded);
    
    EXPECT_EQ(decoded.numbers.size(), 10000);
    EXPECT_EQ(decoded.numbers[0], 42);
    EXPECT_EQ(decoded.numbers[9999], 42);
}

TEST(SerializationTest, ZeroValues) {
    AllTypesMessage original;
    original.int_val = 0;
    original.long_val = 0;
    original.double_val = 0.0;
    original.str_val = "";
    original.vec_val = {};
    
    CMString serialized;
    FLY_ENCODE(original, serialized);
    
    AllTypesMessage decoded;
    FLY_DECODE(serialized, AllTypesMessage, decoded);
    
    EXPECT_EQ(decoded.int_val, 0);
    EXPECT_EQ(decoded.long_val, 0);
    EXPECT_DOUBLE_EQ(decoded.double_val, 0.0);
    EXPECT_TRUE(decoded.str_val.empty());
    EXPECT_TRUE(decoded.vec_val.empty());
}