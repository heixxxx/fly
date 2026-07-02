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

