#include <gtest/gtest.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/compression_utils.h>
#include <storage/cpp/lz4_compressor.h>
#include <storage/cpp/zlib_compressor.h>
#include <storage/cpp/zstd_compressor.h>
#include <fstream>

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
        compressor_ = std::make_unique<Lz4Compressor>();
    }
    std::unique_ptr<Lz4Compressor> compressor_;
};

TEST_F(Lz4CompressorTest, CompressAndDecompress) {
    CMString input = "Hello, LZ4 compression! This is a test string with some repetition. repetition. repetition.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, 0);

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(Lz4CompressorTest, LargeDataRoundTrip) {
    CMString input(100000, 'x');
    input.append(100000, 'y');

    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, StreamingChunkRoundTrip) {
    CMString input(50000, 'a');
    input.append(50000, 'b');

    auto chunk = compressor_->compress_chunk(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress_chunk(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(Lz4CompressorTest, FactoryCreatesLz4) {
    auto comp = CompressorFactory::create(CompressionType::LZ4);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::LZ4);
    EXPECT_EQ(comp->name(), "lz4");

    CMString input = "factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

class ZlibCompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = std::make_unique<ZlibCompressor>();
    }
    std::unique_ptr<ZlibCompressor> compressor_;
};

TEST_F(ZlibCompressorTest, CompressAndDecompress) {
    CMString input = "Hello, ZLIB compression! Testing with repetitive data. Testing with repetitive data.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZlibCompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, 0);

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(ZlibCompressorTest, LargeDataRoundTrip) {
    CMString input(200000, 'z');

    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZlibCompressorTest, FactoryCreatesZlib) {
    auto comp = CompressorFactory::create(CompressionType::ZLIB);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::ZLIB);

    CMString input = "zlib factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

class ZstdCompressorTest : public ::testing::Test {
protected:
    void SetUp() override {
        compressor_ = std::make_unique<ZstdCompressor>();
    }
    std::unique_ptr<ZstdCompressor> compressor_;
};

TEST_F(ZstdCompressorTest, CompressAndDecompress) {
    CMString input = "Hello, ZSTD compression! High compression ratio test data here.";
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZstdCompressorTest, EmptyInput) {
    CMString input;
    auto chunk = compressor_->compress(input);
    EXPECT_EQ(chunk.uncompressed_size, 0);

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result.size(), 0u);
}

TEST_F(ZstdCompressorTest, LargeDataRoundTrip) {
    CMString input(200000, 'q');

    auto chunk = compressor_->compress(input);
    EXPECT_LT(chunk.compressed_size, static_cast<int32_t>(input.size()));

    auto result = compressor_->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST_F(ZstdCompressorTest, FactoryCreatesZstd) {
    auto comp = CompressorFactory::create(CompressionType::ZSTD);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->type(), CompressionType::ZSTD);

    CMString input = "zstd factory test";
    auto chunk = comp->compress(input);
    auto result = comp->decompress(chunk.uncompressed_size, chunk.data);
    EXPECT_EQ(result, input);
}

TEST(CompressionUtilsTest, SerializeDeserializeChunk) {
    auto compressor = CompressorFactory::create(CompressionType::LZ4);
    CMString input = "Test data for serialize/deserialize";
    auto chunk = compressor->compress(input);

    CMString serialized = compression_utils::serialize_chunk(chunk);
    EXPECT_FALSE(serialized.empty());

    int64_t offset = 0;
    auto deserialized = compression_utils::deserialize_chunk(serialized, offset);

    EXPECT_EQ(deserialized.uncompressed_size, chunk.uncompressed_size);
    EXPECT_EQ(deserialized.compressed_size, chunk.compressed_size);
    EXPECT_EQ(deserialized.data, chunk.data);

    auto result = compressor->decompress(deserialized.uncompressed_size, deserialized.data);
    EXPECT_EQ(result, input);
}

TEST(CompressionUtilsTest, RoundTripThroughFile) {
    auto compressor = CompressorFactory::create(CompressionType::LZ4);
    CMString input = "Test data for file I/O round trip";
    auto chunk = compressor->compress(input);

    CMString test_file = "/tmp/fly_compression_test.dat";
    std::ofstream ofs(test_file, std::ios::binary);
    int64_t bytes_written = compression_utils::write_compressed_to_stream(chunk, ofs);
    ofs.close();

    std::ifstream ifs(test_file, std::ios::binary);
    auto read_chunk = compression_utils::read_compressed_from_stream(ifs, 0);
    ifs.close();

    EXPECT_EQ(read_chunk.uncompressed_size, chunk.uncompressed_size);
    EXPECT_EQ(read_chunk.compressed_size, chunk.compressed_size);
    EXPECT_EQ(read_chunk.data, chunk.data);

    auto result = compressor->decompress(read_chunk.uncompressed_size, read_chunk.data);
    EXPECT_EQ(result, input);

    std::remove(test_file.c_str());
}