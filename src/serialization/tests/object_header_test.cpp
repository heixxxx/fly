#include <gtest/gtest.h>
#include <serialization/cpp/object_header.h>

TEST(ObjectHeaderTest, DefaultValues) {
    ObjectHeader header;
    EXPECT_EQ(header.magic_, FLY_OBJECT_MAGIC);
    EXPECT_EQ(header.version_, 1);
    EXPECT_EQ(header.py_name_len_, 0);
    EXPECT_TRUE(header.py_name_.empty());
    EXPECT_EQ(header.total_size_, 0);
    EXPECT_EQ(header.chunk_count_, 0);
    EXPECT_EQ(header.compression_type_, 0);
}

TEST(ObjectHeaderTest, FixedHeaderSize) {
    EXPECT_EQ(ObjectHeader::fixed_header_size(), 20);
}

TEST(ObjectHeaderTest, SerializeDeserializeNoPyName) {
    ObjectHeader header;
    header.total_size_ = 1024;
    header.chunk_count_ = 3;
    header.compression_type_ = 1;

    CMString serialized = header.serialize();
    EXPECT_EQ(serialized.size(), static_cast<size_t>(ObjectHeader::fixed_header_size()));

    int64_t offset = 0;
    ObjectHeader decoded;
    ASSERT_TRUE(ObjectHeader::deserialize(serialized, offset, decoded));
    EXPECT_EQ(offset, ObjectHeader::fixed_header_size());
    EXPECT_EQ(decoded.magic_, FLY_OBJECT_MAGIC);
    EXPECT_EQ(decoded.version_, 1);
    EXPECT_EQ(decoded.py_name_len_, 0);
    EXPECT_TRUE(decoded.py_name_.empty());
    EXPECT_EQ(decoded.total_size_, 1024);
    EXPECT_EQ(decoded.chunk_count_, 3);
    EXPECT_EQ(decoded.compression_type_, 1);
}

TEST(ObjectHeaderTest, SerializeDeserializeWithPyName) {
    ObjectHeader header;
    header.py_name_ = "SomeClass";
    header.total_size_ = 2048;
    header.chunk_count_ = 1;
    header.compression_type_ = 2;

    CMString serialized = header.serialize();
    EXPECT_EQ(serialized.size(), static_cast<size_t>(ObjectHeader::fixed_header_size() + 9));

    int64_t offset = 0;
    ObjectHeader decoded;
    ASSERT_TRUE(ObjectHeader::deserialize(serialized, offset, decoded));
    EXPECT_EQ(offset, ObjectHeader::fixed_header_size() + 9);
    EXPECT_EQ(decoded.py_name_, "SomeClass");
    EXPECT_EQ(decoded.py_name_len_, 9);
    EXPECT_EQ(decoded.total_size_, 2048);
    EXPECT_EQ(decoded.chunk_count_, 1);
    EXPECT_EQ(decoded.compression_type_, 2);
}

TEST(ObjectHeaderTest, IsValid) {
    ObjectHeader header;
    EXPECT_TRUE(header.is_valid());

    ObjectHeader bad;
    bad.magic_ = 0xFFFFFFFF;
    EXPECT_FALSE(bad.is_valid());
}

TEST(ObjectHeaderTest, DeserializeInsufficientData) {
    CMString short_data(5, '\0');
    int64_t offset = 0;
    ObjectHeader decoded;
    EXPECT_FALSE(ObjectHeader::deserialize(short_data, offset, decoded));
}

TEST(ObjectHeaderTest, DeserializeFutureVersion) {
    ObjectHeader header;
    header.version_ = 99;
    CMString serialized = header.serialize();

    int64_t offset = 0;
    ObjectHeader decoded;
    EXPECT_FALSE(ObjectHeader::deserialize(serialized, offset, decoded));
}

TEST(ObjectHeaderTest, RoundTripCppOnly) {
    ObjectHeader header;
    header.total_size_ = 65536;
    header.chunk_count_ = 16;
    header.compression_type_ = 3;

    CMString serialized = header.serialize();
    int64_t offset = 0;
    ObjectHeader decoded;
    ASSERT_TRUE(ObjectHeader::deserialize(serialized, offset, decoded));

    EXPECT_EQ(decoded.total_size_, 65536);
    EXPECT_EQ(decoded.chunk_count_, 16);
    EXPECT_EQ(decoded.compression_type_, 3);
    EXPECT_TRUE(decoded.py_name_.empty());
}

TEST(ObjectHeaderTest, RoundTripPythonClass) {
    ObjectHeader header;
    header.py_name_ = "MyTask";
    header.total_size_ = 4096;
    header.chunk_count_ = 1;
    header.compression_type_ = 0;

    CMString serialized = header.serialize();
    int64_t offset = 0;
    ObjectHeader decoded;
    ASSERT_TRUE(ObjectHeader::deserialize(serialized, offset, decoded));

    EXPECT_EQ(decoded.py_name_, "MyTask");
    EXPECT_EQ(decoded.total_size_, 4096);
    EXPECT_EQ(decoded.chunk_count_, 1);
}

TEST(ObjectHeaderTest, DeserializeBadMagic) {
    ObjectHeader header;
    header.total_size_ = 1;
    CMString serialized = header.serialize();
    // 破坏 magic 前 4 字节（version 等保持合法）→ 返回 false 而非抛异常。
    serialized[0] = '\xff';
    serialized[1] = '\xff';

    int64_t offset = 0;
    ObjectHeader decoded;
    EXPECT_FALSE(ObjectHeader::deserialize(serialized, offset, decoded));
}
