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

// A small payload written through an LZ4 FlyStream skips compression: the
// recorded header.compression_type_ becomes NONE, and reading it back via
// FlyStream still roundtrips correctly (read-side transparently handles NONE).
TEST(FlyStreamBasicTest, SmallPayloadSkipsCompression) {
    std::string payload = "hello small world";  // 17 bytes < default 4096 threshold
    FlyStream w(CompressionType::LZ4, 4194304, "SmallObj");
    w.write(payload.data(), payload.size());
    w.flush();
    auto buf = w.finish_write();
    ASSERT_NE(buf, nullptr);

    // Header.compression_type_ must reflect the actual (skipped) format.
    int64_t off = 0;
    ObjectHeader hdr;
    ASSERT_TRUE(ObjectHeader::deserialize({buf->data(), buf->size()}, off, hdr));
    EXPECT_EQ(static_cast<CompressionType>(hdr.compression_type_), CompressionType::NONE);
    EXPECT_EQ(hdr.py_name_, "SmallObj");

    // Read back via FlyStream: NONE path still returns the original bytes.
    FlyStream r(buf);
    CMString recovered = r.read(payload.size());
    EXPECT_EQ(std::string(recovered.data(), recovered.size()), payload);
}

// A large payload through an LZ4 FlyStream is actually compressed, so the
// header keeps compression_type_ = LZ4.
TEST(FlyStreamBasicTest, LargePayloadStillCompresses) {
    std::string payload(10000, 'X');
    FlyStream w(CompressionType::LZ4, 4194304, "BigObj");
    w.write(payload.data(), payload.size());
    w.flush();
    auto buf = w.finish_write();

    int64_t off = 0;
    ObjectHeader hdr;
    ASSERT_TRUE(ObjectHeader::deserialize({buf->data(), buf->size()}, off, hdr));
    EXPECT_EQ(static_cast<CompressionType>(hdr.compression_type_), CompressionType::LZ4);

    FlyStream r(buf);
    CMString recovered = r.read(payload.size());
    EXPECT_EQ(std::string(recovered.data(), recovered.size()), payload);
}
