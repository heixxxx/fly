#include <gtest/gtest.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/compressor.h>
#include <sstream>

class CompressingStreamBufTest : public ::testing::TestWithParam<CompressionType> {};

TEST_P(CompressingStreamBufTest, CompressAndCount) {
    CompressionType type = GetParam();
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(type);

    CompressingStreamBuf buf(oss, CompressorFactory::create(type), 64);
    std::ostream os(&buf);

    std::string data(200, 'A');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 200);
    EXPECT_GE(buf.chunk_count(), 3);
    EXPECT_GT(oss.str().size(), 0);
}

INSTANTIATE_TEST_SUITE_P(CompressionTypes, CompressingStreamBufTest,
    ::testing::Values(CompressionType::LZ4, CompressionType::ZLIB, CompressionType::ZSTD));

TEST(CompressingStreamBufBasicTest, NoCompression) {
    std::ostringstream oss;
    CompressingStreamBuf buf(oss, nullptr, 64);
    std::ostream os(&buf);

    std::string data(100, 'B');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 100);
    EXPECT_EQ(buf.chunk_count(), 2);
}

TEST(CompressingStreamBufBasicTest, SmallDataSingleChunk) {
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(CompressionType::LZ4);

    CompressingStreamBuf buf(oss, std::move(compressor), 1024);
    std::ostream os(&buf);

    std::string data(50, 'C');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 50);
    EXPECT_EQ(buf.chunk_count(), 1);
}

TEST(CompressingStreamBufBasicTest, ExactChunkSize) {
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(CompressionType::LZ4);

    size_t chunk_size = 128;
    CompressingStreamBuf buf(oss, std::move(compressor), static_cast<int64_t>(chunk_size));
    std::ostream os(&buf);

    std::string data(chunk_size, 'D');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), static_cast<int64_t>(chunk_size));
    EXPECT_EQ(buf.chunk_count(), 1);
}

TEST(CompressingStreamBufBasicTest, MultipleFlushes) {
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(CompressionType::LZ4);

    CompressingStreamBuf buf(oss, std::move(compressor), 100);
    std::ostream os(&buf);

    os.write("AAAA", 4);
    os.write("BBBB", 4);
    os.write("CCCC", 4);
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 12);
    EXPECT_EQ(buf.chunk_count(), 1);
}

TEST(CompressingStreamBufBasicTest, LargeDataMultipleChunks) {
    std::ostringstream oss;
    auto compressor = CompressorFactory::create(CompressionType::LZ4);

    CompressingStreamBuf buf(oss, std::move(compressor), 1024);
    std::ostream os(&buf);

    std::string data(5000, 'E');
    os.write(data.data(), static_cast<std::streamsize>(data.size()));
    os.flush();

    EXPECT_EQ(buf.total_uncompressed(), 5000);
    EXPECT_GE(buf.chunk_count(), 4);
}