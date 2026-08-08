#include <gtest/gtest.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/lz4_compressor.h>
#include <storage/cpp/zlib_compressor.h>
#include <storage/cpp/zstd_compressor.h>

TEST(CompressionTypeTest, FromName) {
    EXPECT_EQ(CompressorFactory::type_from_name("none"), CompressionType::NONE);
    EXPECT_EQ(CompressorFactory::type_from_name("lz4"), CompressionType::LZ4);
    EXPECT_EQ(CompressorFactory::type_from_name("zlib"), CompressionType::ZLIB);
    EXPECT_EQ(CompressorFactory::type_from_name("zstd"), CompressionType::ZSTD);
}

TEST(CompressionTypeTest, NameFromType) {
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::NONE), "none");
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::LZ4), "lz4");
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::ZLIB), "zlib");
    EXPECT_EQ(CompressorFactory::name_from_type(CompressionType::ZSTD), "zstd");
}

TEST(CompressorFactoryTest, CreateByType) {
    auto lz4 = CompressorFactory::create(CompressionType::LZ4);
    ASSERT_NE(lz4, nullptr);
    EXPECT_EQ(lz4->type(), CompressionType::LZ4);
    EXPECT_EQ(lz4->name(), "lz4");

    auto zlib_comp = CompressorFactory::create(CompressionType::ZLIB);
    ASSERT_NE(zlib_comp, nullptr);
    EXPECT_EQ(zlib_comp->type(), CompressionType::ZLIB);

    auto zstd_comp = CompressorFactory::create(CompressionType::ZSTD);
    ASSERT_NE(zstd_comp, nullptr);
    EXPECT_EQ(zstd_comp->type(), CompressionType::ZSTD);

    auto none_comp = CompressorFactory::create(CompressionType::NONE);
    ASSERT_NE(none_comp, nullptr);
    EXPECT_EQ(none_comp->type(), CompressionType::NONE);
}

TEST(CompressorFactoryTest, CreateByName) {
    auto lz4 = CompressorFactory::create_from_name("lz4");
    ASSERT_NE(lz4, nullptr);
    EXPECT_EQ(lz4->type(), CompressionType::LZ4);

    auto none_comp = CompressorFactory::create_from_name("none");
    ASSERT_NE(none_comp, nullptr);
    EXPECT_EQ(none_comp->type(), CompressionType::NONE);
}

class Lz4CompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = CMMakeUnique<Lz4Compressor>();
    }
    CMUniquePtr<Lz4Compressor> compressor_;
};

TEST_F(Lz4CompressorTest, CompressAndDecompress) {
    CMString input = "Hello, LZ4 compression! This is a test string with some repetition. repetition. repetition.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, static_cast<int32_t>(input.size()));
    EXPECT_LT(chunk.compressed_size_, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, 0);

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(Lz4CompressorTest, LargeDataRoundTrip) {
    CMString input(100000, 'x');
    input.append(100000, 'y');

    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size_, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, FactoryCreatesLz4) {
    auto comp = CompressorFactory::create(CompressionType::LZ4);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::LZ4);
    EXPECT_EQ(comp->name(), "lz4");

    CMString input = "factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

class ZlibCompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = CMMakeUnique<ZlibCompressor>();
    }
    CMUniquePtr<ZlibCompressor> compressor_;
};

TEST_F(ZlibCompressorTest, CompressAndDecompress) {
    CMString input = "Hello, ZLIB compression! Testing with repetitive data. Testing with repetitive data.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, static_cast<int32_t>(input.size()));
    EXPECT_LT(chunk.compressed_size_, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST_F(ZlibCompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, 0);

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(ZlibCompressorTest, LargeDataRoundTrip) {
    CMString input(200000, 'z');

    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size_, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST_F(ZlibCompressorTest, FactoryCreatesZlib) {
    auto comp = CompressorFactory::create(CompressionType::ZLIB);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::ZLIB);

    CMString input = "zlib factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

class ZstdCompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = CMMakeUnique<ZstdCompressor>();
    }
    CMUniquePtr<ZstdCompressor> compressor_;
};

TEST_F(ZstdCompressorTest, CompressAndDecompress) {
    CMString input = "Hello, ZSTD compression! High compression ratio test data here.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST_F(ZstdCompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, 0);

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(ZstdCompressorTest, LargeDataRoundTrip) {
    CMString input(200000, 'q');

    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size_, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST_F(ZstdCompressorTest, FactoryCreatesZstd) {
    auto comp = CompressorFactory::create(CompressionType::ZSTD);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::ZSTD);

    CMString input = "zstd factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST(CompressorFactoryTest, TypeFromNameUnknown) {
    EXPECT_EQ(CompressorFactory::type_from_name("unknown"), CompressionType::NONE);
}

TEST(CompressorFactoryTest, NameFromTypeInvalid) {
    EXPECT_EQ(CompressorFactory::name_from_type(static_cast<CompressionType>(999)), "unknown");
}

TEST(CompressorFactoryTest, CreateFromNameZlibZstd) {
    auto zlib = CompressorFactory::create_from_name("zlib");
    ASSERT_NE(zlib, nullptr);
    EXPECT_EQ(zlib->type(), CompressionType::ZLIB);

    auto zstd = CompressorFactory::create_from_name("zstd");
    ASSERT_NE(zstd, nullptr);
    EXPECT_EQ(zstd->type(), CompressionType::ZSTD);
}

TEST_F(ZstdCompressorTest, DecompressGarbageData) {
    CMString garbage_data = CMString(100, '\xff');
    EXPECT_TRUE(compressor_->decompress(100, garbage_data).empty());
}

TEST_F(ZlibCompressorTest, DecompressGarbageData) {
    CMString garbage_data = CMString(100, '\xff');
    EXPECT_TRUE(compressor_->decompress(100, garbage_data).empty());
}

TEST_F(ZstdCompressorTest, CustomCompressionLevels) {
    auto level1 = CMMakeUnique<ZstdCompressor>(1);
    auto level19 = CMMakeUnique<ZstdCompressor>(19);

    CMString input(50000, 'z');

    auto chunk1 = level1->compress(input);
    auto result1 = level1->decompress(chunk1.uncompressed_size_, chunk1.data_);
    EXPECT_EQ(result1, input);

    auto chunk19 = level19->compress(input);
    auto result19 = level19->decompress(chunk19.uncompressed_size_, chunk19.data_);
    EXPECT_EQ(result19, input);
}

TEST_F(ZlibCompressorTest, CustomCompressionLevels) {
    auto level1 = CMMakeUnique<ZlibCompressor>(1);
    auto level9 = CMMakeUnique<ZlibCompressor>(9);

    CMString input(50000, 'z');

    auto chunk1 = level1->compress(input);
    auto result1 = level1->decompress(chunk1.uncompressed_size_, chunk1.data_);
    EXPECT_EQ(result1, input);

    auto chunk9 = level9->compress(input);
    auto result9 = level9->decompress(chunk9.uncompressed_size_, chunk9.data_);
    EXPECT_EQ(result9, input);
}

TEST(CompressorFactoryTest, NoneCompressorCompressAndDecompress) {
    auto comp = CompressorFactory::create(CompressionType::NONE);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::NONE);
    EXPECT_EQ(comp->name(), "none");

    CMString input = "none compressor test data";
    auto chunk = comp->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, static_cast<int32_t>(input.size()));
    EXPECT_EQ(chunk.compressed_size_, static_cast<int32_t>(input.size()));
    EXPECT_EQ(chunk.data_, input);

    auto result = comp->decompress(chunk.uncompressed_size_, chunk.data_);
    EXPECT_EQ(result, input);
}

TEST(CompressorFactoryTest, NoneCompressorEmptyInput) {
    auto comp = CompressorFactory::create(CompressionType::NONE);
    ASSERT_NE(comp, nullptr);

    CMString input;
    auto chunk = comp->compress(input);
    EXPECT_EQ(chunk.uncompressed_size_, 0);
    EXPECT_EQ(chunk.compressed_size_, 0);

    auto result = comp->decompress(0, chunk.data_);
    EXPECT_EQ(result.size(), 0u);
}

TEST(CompressorFactoryTest, NoneCompressorFromName) {
    auto comp = CompressorFactory::create_from_name("none");
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::NONE);
}

TEST(CompressorFactoryTest, CreateWithInvalidEnumReturnsNull) {
    auto comp = CompressorFactory::create(static_cast<CompressionType>(100));
    EXPECT_EQ(comp, nullptr);
}

TEST_F(Lz4CompressorTest, DecompressGarbageData) {
    CMString garbage_data = CMString(100, '\xff');
    EXPECT_TRUE(compressor_->decompress(100, garbage_data).empty());
}

// ── compression_level 透传测试 ──────────────────────────────────────
//
// Bug B4: CompressorFactory::create(type) 不接受 level 参数，database.cpp 读取的
// compression_level_ 配置写后不读，各 Compressor 用硬编码默认值。用户调
// compression_level 配置静默不生效。
//
// 修复: CompressorFactory::create(type, level) 透传 level 给各 Compressor。
// 验证: 同一输入、不同 level → 不同的 compressed_size（证明 level 实际生效）。

// 构造可压缩但 level 敏感的输入：半结构化数据（伪随机字节 + 重复模式混合），
// 使 zlib/zstd 在不同 level 下压缩率有可观测差异。
// 纯重复数据（如 "ABCDE" 循环）对 zstd 即使 level=1 也压到极限，无法区分 level。
static CMString make_compressible_input(size_t size) {
    CMString input;
    input.reserve(size);
    uint32_t state = 12345;
    for (size_t i = 0; i < size; ++i) {
        // LCG 伪随机 + 每 7 字节插入一次重复的 ASCII 块
        state = state * 1103515245u + 12345u;
        char c = (i % 7 == 0) ? char('A' + (i / 7) % 26)
                              : char('a' + (state >> 16) % 26);
        input.push_back(c);
    }
    return input;
}

TEST(CompressorFactoryTest, ZlibLevelAffectsCompression) {
    // Bug 复现: factory 带 level 重载，不同 level 应产生不同压缩大小。
    // zlib level 1（最快）vs level 9（最好）压缩率差异显著。
    auto input = make_compressible_input(16384);

    auto c1 = CompressorFactory::create(CompressionType::ZLIB, 1);
    auto c9 = CompressorFactory::create(CompressionType::ZLIB, 9);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c9, nullptr);

    auto chunk1 = c1->compress(input);
    auto chunk9 = c9->compress(input);

    // level 9 应比 level 1 压得更小（对高度重复的输入差异显著）。
    EXPECT_GT(chunk1.compressed_size_, chunk9.compressed_size_)
        << "level=1 (" << chunk1.compressed_size_ << ") should be larger than "
        << "level=9 (" << chunk9.compressed_size_ << ")";

    // 两者都应能正确解压回原数据。
    EXPECT_EQ(c1->decompress(chunk1.uncompressed_size_, chunk1.data_), input);
    EXPECT_EQ(c9->decompress(chunk9.uncompressed_size_, chunk9.data_), input);
}

TEST(CompressorFactoryTest, ZstdLevelAffectsCompression) {
    // 用带明确重复子串的数据（zstd 高 level 能发现更远的匹配）。
    // 构造: 大量重复段落（每段 256 字节，段落间有部分重叠）。
    CMString input;
    input.reserve(32768);
    for (int block = 0; block < 128; ++block) {
        // 每 256 字节一个块，块内有 200 字节相同 + 56 字节变化
        char c = char('A' + block % 20);
        for (int i = 0; i < 200; ++i) input.push_back(c);
        for (int i = 0; i < 56; ++i) input.push_back(char('a' + (block + i) % 26));
    }

    auto c1 = CompressorFactory::create(CompressionType::ZSTD, 1);
    auto c19 = CompressorFactory::create(CompressionType::ZSTD, 19);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c19, nullptr);

    auto chunk1 = c1->compress(input);
    auto chunk19 = c19->compress(input);

    EXPECT_GT(chunk1.compressed_size_, chunk19.compressed_size_)
        << "zstd level=1 (" << chunk1.compressed_size_ << ") should be larger than "
        << "level=19 (" << chunk19.compressed_size_ << ")";

    EXPECT_EQ(c1->decompress(chunk1.uncompressed_size_, chunk1.data_), input);
    EXPECT_EQ(c19->decompress(chunk19.uncompressed_size_, chunk19.data_), input);
}

TEST(CompressorFactoryTest, DefaultLevelStillWorks) {
    // level=-1（默认）应与现有行为一致，不破坏未改动的调用点。
    auto comp = CompressorFactory::create(CompressionType::ZLIB);
    ASSERT_NE(comp, nullptr);

    auto input = make_compressible_input(256);
    auto chunk = comp->compress(input);
    EXPECT_EQ(comp->decompress(chunk.uncompressed_size_, chunk.data_), input);
}


