#include <gtest/gtest.h>
#include <common/serialization/cpp/object_header.h>

TEST(ObjectHeaderTest, DefaultValues) {
    ObjectHeader header;
    EXPECT_EQ(header.magic_, FLY_OBJECT_MAGIC);
    EXPECT_EQ(header.version_, 1);
    EXPECT_EQ(header.py_name_len_, 0);
    EXPECT_TRUE(header.py_name_.empty());
    EXPECT_EQ(header.total_size_, 0);
    EXPECT_EQ(header.chunk_count_, 0);
    EXPECT_EQ(header.compression_type_, CompressionType::NONE);
}

TEST(ObjectHeaderTest, FixedHeaderSize) {
    EXPECT_EQ(ObjectHeader::fixed_header_size(), 24);  // v2: +block_table_len(u32)
}

TEST(ObjectHeaderTest, SerializeDeserializeNoPyName) {
    ObjectHeader header;
    header.total_size_ = 1024;
    header.chunk_count_ = 3;
    header.compression_type_ = CompressionType::LZ4;

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
    EXPECT_EQ(decoded.compression_type_, CompressionType::LZ4);
}

TEST(ObjectHeaderTest, SerializeDeserializeWithPyName) {
    ObjectHeader header;
    header.py_name_ = "SomeClass";
    header.total_size_ = 2048;
    header.chunk_count_ = 1;
    header.compression_type_ = CompressionType::ZLIB;

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
    EXPECT_EQ(decoded.compression_type_, CompressionType::ZLIB);
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
    header.compression_type_ = CompressionType::ZSTD;

    CMString serialized = header.serialize();
    int64_t offset = 0;
    ObjectHeader decoded;
    ASSERT_TRUE(ObjectHeader::deserialize(serialized, offset, decoded));

    EXPECT_EQ(decoded.total_size_, 65536);
    EXPECT_EQ(decoded.chunk_count_, 16);
    EXPECT_EQ(decoded.compression_type_, CompressionType::ZSTD);
    EXPECT_TRUE(decoded.py_name_.empty());
}

TEST(ObjectHeaderTest, RoundTripPythonClass) {
    ObjectHeader header;
    header.py_name_ = "MyTask";
    header.total_size_ = 4096;
    header.chunk_count_ = 1;
    header.compression_type_ = CompressionType::NONE;

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

// ── CompressionType 定型（批次 C 第二程 P1-1）──────────────────────────
// 字节级红线：盘上 fixed 段/trailer 的压缩类型域恒 1 字节 0-3 直通。
// 枚举底层类型 int8_t → uint8_t 定型与字段类型化不得改变任何字节。

TEST(ObjectHeaderTest, CompressionFieldIsSingleByteWireValue) {
    // fixed 布局：[magic 4][version 1][py_name_len 2][block_table_len 4]
    //             [total 8][chunk_count 4][comp_type 1] —— 末字节即压缩类型。
    // 逐值断言盘面字节（历史格式锁定：LZ4=1/ZLIB=2/ZSTD=3 直通）。
    const uint8_t expected[] = {0x00, 0x01, 0x02, 0x03};
    for (uint8_t raw = 0; raw < 4; ++raw) {
        ObjectHeader header;
        header.chunk_count_ = 1;
        header.compression_type_ = static_cast<CompressionType>(raw);
        CMString serialized = header.serialize();
        ASSERT_EQ(serialized.size(), static_cast<size_t>(ObjectHeader::fixed_header_size()));
        EXPECT_EQ(static_cast<uint8_t>(serialized[23]), expected[raw])
            << "comp_type byte drift at raw=" << static_cast<int>(raw);
    }
    // 枚举定型后仍为 1 字节（同宽保证）。
    EXPECT_EQ(sizeof(CompressionType), 1u);
}

TEST(ObjectHeaderTest, TrailerCompressionByteMatchesFixedByte) {
    // trailer（v2）与 fixed 段同 compression_type 域：同 header 两条序列化
    // 路径的该字节必须一致（读侧 trailer 解析与 L3 META 供值同源）。
    ObjectHeader header;
    header.py_name_ = "T";
    header.total_size_ = 128;
    header.chunk_count_ = 2;
    header.compression_type_ = CompressionType::LZ4;
    header.block_comp_lens_ = {16, 16};

    CMString fixed = header.serialize();
    CMString trailer = header.serialize_trailer();
    ASSERT_EQ(static_cast<size_t>(fixed[23]), 0x01);
    // trailer 布局：[块表 8][py_name 1][fixed 24][crc 8]——fixed 段末字节
    // 位于 trailer 尾部 crc 之前 1 字节。
    ASSERT_GE(trailer.size(), 8u + 1u + 24u + 8u);
    EXPECT_EQ(static_cast<uint8_t>(trailer[trailer.size() - 8 - 1]), 0x01);
}

TEST(ObjectHeaderTest, DeserializeRejectsOutOfRangeCompressionMemoryPath) {
    // 内存路径负例：comp_type 越界（4 = 未定义枚举值）→ 确定性拒绝。
    ObjectHeader header;
    header.chunk_count_ = 1;
    header.compression_type_ = CompressionType::ZSTD;
    CMString serialized = header.serialize();
    serialized[23] = static_cast<char>(4);

    int64_t offset = 0;
    ObjectHeader decoded;
    EXPECT_FALSE(ObjectHeader::deserialize(serialized, offset, decoded));
    // 合法边界值不误伤：3（ZSTD）仍通过。
    serialized[23] = static_cast<char>(3);
    offset = 0;
    ASSERT_TRUE(ObjectHeader::deserialize(serialized, offset, decoded));
    EXPECT_EQ(decoded.compression_type_, CompressionType::ZSTD);
}

TEST(ObjectHeaderTest, DeserializeTrailerRejectsOutOfRangeCompression) {
    // trailer 路径负例：越界压缩类型 = 数据损坏，按零容忍拒绝（trailer
    // 解析失败），不得以未定义枚举值流入解压管线。
    ObjectHeader header;
    header.py_name_ = "X";
    header.total_size_ = 64;
    header.chunk_count_ = 1;
    header.compression_type_ = CompressionType::LZ4;
    header.block_comp_lens_ = {8};
    CMString record_tail = header.serialize_trailer();

    // 记录布局：[块流区][trailer]——trailer 解析只看尾部，前置块流字节任意。
    CMString record = CMString(16, '\x00') + record_tail;
    record[record.size() - 8 - 1] = static_cast<char>(200);  // 越界值

    ObjectHeader decoded;
    size_t trailer_len = 0;
    EXPECT_FALSE(ObjectHeader::deserialize_trailer(record, decoded, trailer_len));
}

TEST(ObjectHeaderTest, IsValidCompressionTypeBoundary) {
    EXPECT_FALSE(is_valid_compression_type(4));    // 首个越界值
    EXPECT_FALSE(is_valid_compression_type(255));
    for (uint8_t raw = 0; raw <= 3; ++raw) {
        EXPECT_TRUE(is_valid_compression_type(raw));
    }
}
