#include <gtest/gtest.h>
#include <serialization/cpp/object_header.h>

TEST(ObjectHeaderTest, DefaultValues) {
    ObjectHeader header;
    EXPECT_EQ(header.magic, FLY_OBJECT_MAGIC);
    EXPECT_EQ(header.version, 1);
    EXPECT_EQ(header.py_name_len, 0);
    EXPECT_TRUE(header.py_name.empty());
    EXPECT_EQ(header.total_size, 0);
    EXPECT_EQ(header.chunk_count, 0);
    EXPECT_EQ(header.compression_type, 0);
}

TEST(ObjectHeaderTest, FixedHeaderSize) {
    EXPECT_EQ(ObjectHeader::fixed_header_size(), 20);
}

TEST(ObjectHeaderTest, SerializeDeserializeNoPyName) {
    ObjectHeader header;
    header.total_size = 1024;
    header.chunk_count = 3;
    header.compression_type = 1;

    CMString serialized = header.serialize();
    EXPECT_EQ(serialized.size(), static_cast<size_t>(ObjectHeader::fixed_header_size()));

    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);
    EXPECT_EQ(offset, ObjectHeader::fixed_header_size());
    EXPECT_EQ(decoded.magic, FLY_OBJECT_MAGIC);
    EXPECT_EQ(decoded.version, 1);
    EXPECT_EQ(decoded.py_name_len, 0);
    EXPECT_TRUE(decoded.py_name.empty());
    EXPECT_EQ(decoded.total_size, 1024);
    EXPECT_EQ(decoded.chunk_count, 3);
    EXPECT_EQ(decoded.compression_type, 1);
}

TEST(ObjectHeaderTest, SerializeDeserializeWithPyName) {
    ObjectHeader header;
    header.py_name = "SomeClass";
    header.total_size = 2048;
    header.chunk_count = 1;
    header.compression_type = 2;

    CMString serialized = header.serialize();
    EXPECT_EQ(serialized.size(), static_cast<size_t>(ObjectHeader::fixed_header_size() + 9));

    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);
    EXPECT_EQ(offset, ObjectHeader::fixed_header_size() + 9);
    EXPECT_EQ(decoded.py_name, "SomeClass");
    EXPECT_EQ(decoded.py_name_len, 9);
    EXPECT_EQ(decoded.total_size, 2048);
    EXPECT_EQ(decoded.chunk_count, 1);
    EXPECT_EQ(decoded.compression_type, 2);
}

TEST(ObjectHeaderTest, IsValid) {
    ObjectHeader header;
    EXPECT_TRUE(header.is_valid());

    ObjectHeader bad;
    bad.magic = 0xFFFFFFFF;
    EXPECT_FALSE(bad.is_valid());
}

TEST(ObjectHeaderTest, DeserializeInsufficientData) {
    CMString short_data(5, '\0');
    int64_t offset = 0;
    EXPECT_THROW(ObjectHeader::deserialize(short_data, offset), std::runtime_error);
}

TEST(ObjectHeaderTest, DeserializeFutureVersion) {
    ObjectHeader header;
    header.version = 99;
    CMString serialized = header.serialize();

    int64_t offset = 0;
    EXPECT_THROW(ObjectHeader::deserialize(serialized, offset), std::runtime_error);
}

TEST(ObjectHeaderTest, RoundTripCppOnly) {
    ObjectHeader header;
    header.total_size = 65536;
    header.chunk_count = 16;
    header.compression_type = 3;

    CMString serialized = header.serialize();
    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);

    EXPECT_EQ(decoded.total_size, 65536);
    EXPECT_EQ(decoded.chunk_count, 16);
    EXPECT_EQ(decoded.compression_type, 3);
    EXPECT_TRUE(decoded.py_name.empty());
}

TEST(ObjectHeaderTest, RoundTripPythonClass) {
    ObjectHeader header;
    header.py_name = "MyTask";
    header.total_size = 4096;
    header.chunk_count = 1;
    header.compression_type = 0;

    CMString serialized = header.serialize();
    int64_t offset = 0;
    ObjectHeader decoded = ObjectHeader::deserialize(serialized, offset);

    EXPECT_EQ(decoded.py_name, "MyTask");
    EXPECT_EQ(decoded.total_size, 4096);
    EXPECT_EQ(decoded.chunk_count, 1);
}