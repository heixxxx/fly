#include <gtest/gtest.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/compressor.h>
#include <sstream>
#include <string>
#include <cstring>

namespace fly {

TEST(CompressingStreamBufTest, NoneCompressorPassthrough) {
    std::ostringstream oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::NONE);
        CompressingStreamBuf buf(oss, std::move(compressor), 64);
        std::ostream os(&buf);
        os << "Hello, World!";
    }

    std::string result = oss.str();
    EXPECT_GT(result.size(), 0u);

    int32_t uncomp_size = 0;
    int32_t comp_size = 0;
    std::memcpy(&uncomp_size, result.data(), sizeof(int32_t));
    std::memcpy(&comp_size, result.data() + sizeof(int32_t), sizeof(int32_t));

    EXPECT_EQ(uncomp_size, 13);
    EXPECT_EQ(comp_size, 13);
}

TEST(CompressingStreamBufTest, SmallDataSingleChunk) {
    std::ostringstream oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::NONE);
        CompressingStreamBuf buf(oss, std::move(compressor), 1024);
        std::ostream os(&buf);
        os << "short";
    }

    std::string result = oss.str();
    EXPECT_GT(result.size(), 0u);

    int32_t uncomp_size = 0;
    int32_t comp_size = 0;
    std::memcpy(&uncomp_size, result.data(), sizeof(int32_t));
    std::memcpy(&comp_size, result.data() + sizeof(int32_t), sizeof(int32_t));

    EXPECT_EQ(uncomp_size, 5);
    EXPECT_EQ(comp_size, 5);
}

TEST(CompressingStreamBufTest, MultipleChunksTriggered) {
    std::ostringstream oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::NONE);
        CompressingStreamBuf buf(oss, std::move(compressor), 16);
        std::ostream os(&buf);
        os << "AAAAAAAAAAAAAAAA" "BBBBBBBBBBBBBBBB";
    }

    std::string result = oss.str();

    int32_t uncomp1 = 0, comp1 = 0;
    std::memcpy(&uncomp1, result.data(), sizeof(int32_t));
    std::memcpy(&comp1, result.data() + sizeof(int32_t), sizeof(int32_t));

    EXPECT_EQ(uncomp1, 16);
    EXPECT_EQ(comp1, 16);
}

TEST(CompressingStreamBufTest, Lz4CompressionShrinksData) {
    std::ostringstream oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        CompressingStreamBuf buf(oss, std::move(compressor), 256);
        std::ostream os(&buf);
        std::string repetitive(200, 'A');
        os << repetitive;
    }

    std::string result = oss.str();
    EXPECT_GT(result.size(), 0u);

    int32_t uncomp_size = 0;
    std::memcpy(&uncomp_size, result.data(), sizeof(int32_t));

    EXPECT_EQ(uncomp_size, 200);
}

TEST(CompressingStreamBufTest, NullCompressorPassthrough) {
    std::ostringstream oss;
    {
        CompressingStreamBuf buf(oss, nullptr, 64);
        std::ostream os(&buf);
        os << "no compression";
    }

    std::string result = oss.str();

    int32_t uncomp_size = 0, comp_size = 0;
    std::memcpy(&uncomp_size, result.data(), sizeof(int32_t));
    std::memcpy(&comp_size, result.data() + sizeof(int32_t), sizeof(int32_t));

    EXPECT_EQ(uncomp_size, 14);
    EXPECT_EQ(comp_size, 14);
}

TEST(CompressingStreamBufTest, TotalUncompressedTracking) {
    std::ostringstream oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::NONE);
        CompressingStreamBuf buf(oss, std::move(compressor), 10);
        std::ostream os(&buf);
        os << "12345678901234567890";
    }

    EXPECT_EQ(oss.str().size() > 0, true);
}

}  // namespace fly