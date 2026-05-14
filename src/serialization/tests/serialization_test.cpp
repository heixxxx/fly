#include <gtest/gtest.h>
#include <serialization/cpp/serialization_macros.h>

struct TestMessage {
    int32_t id;
    std::string name;
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
    
    std::vector<unsigned char> serialized;
    FLY_ENCODE_TO_BYTES(original, serialized);
    
    TestMessage decoded;
    FLY_DECODE_FROM_BYTES(serialized, TestMessage, decoded);
    
    EXPECT_EQ(decoded.id, original.id);
    EXPECT_EQ(decoded.name, original.name);
}