#include <gtest/gtest.h>
#include <storage/cpp/fly_stream.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <serialization/cpp/object_header.h>
#include <cstring>
#include <string>
#include <vector>

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
    // （trailer 格式：从尾部解析，§4.4）
    ObjectHeader hdr;
    size_t trailer_len = 0;
    ASSERT_TRUE(ObjectHeader::deserialize_trailer({buf->data(), buf->size()}, hdr, trailer_len));
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

    ObjectHeader hdr;
    size_t trailer_len = 0;
    ASSERT_TRUE(ObjectHeader::deserialize_trailer({buf->data(), buf->size()}, hdr, trailer_len));
    EXPECT_EQ(static_cast<CompressionType>(hdr.compression_type_), CompressionType::LZ4);

    FlyStream r(buf);
    CMString recovered = r.read(payload.size());
    EXPECT_EQ(std::string(recovered.data(), recovered.size()), payload);
}

// 读模式 API：FlyStream(FlyBufferPtr) 的 read_all / readline / readinto。
TEST(FlyStreamBasicTest, ReadModeAllLineReadinto) {
    std::string payload = "line-one\nline-two\ntail-without-newline";
    FlyStream w(CompressionType::NONE, 4096, "bytes");
    w.write(payload.data(), payload.size());
    w.flush();
    auto buf = w.finish_write();

    FlyStream r(buf);
    // readline 含换行符（逐字节 readinto 驱动）。
    CMString l1 = r.readline();
    EXPECT_EQ(std::string(l1.data(), l1.size()), "line-one\n");
    CMString l2 = r.readline();
    EXPECT_EQ(std::string(l2.data(), l2.size()), "line-two\n");

    // readinto：定长读 4 字节。
    char tmp[4] = {0};
    EXPECT_EQ(r.readinto(tmp, sizeof(tmp)), 4u);
    EXPECT_EQ(std::string(tmp, 4), "tail");

    // read_all：读完剩余。
    CMString rest = r.read_all();
    EXPECT_EQ(std::string(rest.data(), rest.size()), "-without-newline");

    // 读空后再 read_all → 空。
    EXPECT_TRUE(r.read_all().empty());
}

TEST(FlyStreamBasicTest, ReadAllRoundtripCompressed) {
    std::string payload(3000, 'z');
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>('a' + (i % 26));
    }
    FlyStream w(CompressionType::LZ4, 1024, "bytes");
    w.write(payload.data(), payload.size());
    w.flush();
    auto buf = w.finish_write();

    FlyStream r(buf);
    CMString all = r.read_all();
    EXPECT_EQ(std::string(all.data(), all.size()), payload);
    EXPECT_EQ(r.py_name(), "bytes");
}
