#include <gtest/gtest.h>
#include <storage/cpp/fly_stream.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <cstring>
#include <string>

class FlyStreamTest : public ::testing::TestWithParam<CompressionType> {};
TEST_P(FlyStreamTest, WriteReadRoundtrip) {
    std::string payload(500, 'X');
    FlyStream writer(GetParam(), 64);
    writer.write(payload.data(), payload.size());
    writer.flush();
    auto buf = writer.finish_write();
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(writer.total_uncompressed(), static_cast<int64_t>(payload.size()));
    FlyStream reader(buf);
    CMString recovered = reader.read(payload.size());
    EXPECT_EQ(std::string(recovered.data(), recovered.size()), payload);
}
INSTANTIATE_TEST_SUITE_P(CompressionTypes, FlyStreamTest,
    ::testing::Values(CompressionType::NONE, CompressionType::LZ4,
                      CompressionType::ZLIB, CompressionType::ZSTD));
TEST(FlyStreamBasicTest, MultipleWrites) {
    FlyStream w(CompressionType::LZ4, 1024);
    w.write("AAAA", 4); w.write("BBBB", 4); w.write("CCCC", 4);
    w.flush(); auto buf = w.finish_write();
    FlyStream r(buf); auto rec = r.read(12);
    EXPECT_EQ(std::string(rec.data(), 12), "AAAABBBBCCCC");
}
