#include <gtest/gtest.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <common/cpp/fly_buffer.h>
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

// Small payloads at or below the compression threshold skip compression and
// are written as raw passthrough chunks (comp_size == uncomp_size == payload),
// even though a real compressor was configured. effective_compression_type()
// then reports NONE so callers can record the actual on-disk format.
TEST(CompressingStreamBufTest, SmallDataSkipsCompression) {
    // 1) Small payload → skipped.
    std::ostringstream small_oss;
    CompressingStreamBuf* small_buf = nullptr;
    {
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        small_buf = new CompressingStreamBuf(small_oss, std::move(compressor),
                                             4194304, 4096);
        std::ostream os(small_buf);
        std::string payload(100, 'A');
        os << payload;
        os.flush();
    }
    EXPECT_EQ(small_buf->effective_compression_type(), CompressionType::NONE);
    delete small_buf;

    std::string small_result = small_oss.str();
    ASSERT_GE(small_result.size(), sizeof(int32_t) * 2 + 100u);
    int32_t uncomp = 0, comp = 0;
    std::memcpy(&uncomp, small_result.data(), sizeof(int32_t));
    std::memcpy(&comp, small_result.data() + sizeof(int32_t), sizeof(int32_t));
    EXPECT_EQ(uncomp, 100);   // raw passthrough: sizes equal payload size
    EXPECT_EQ(comp, 100);
    // Payload bytes preserved verbatim.
    EXPECT_EQ(std::string(small_result.data() + sizeof(int32_t) * 2, 100),
              std::string(100, 'A'));

    // 2) Large payload → actually compressed (comp_size < uncomp_size for
    //    repetitive data), effective type stays LZ4.
    std::ostringstream large_oss;
    CompressingStreamBuf* large_buf = nullptr;
    {
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        large_buf = new CompressingStreamBuf(large_oss, std::move(compressor),
                                             4194304, 4096);
        std::ostream os(large_buf);
        std::string repetitive(5000, 'A');
        os << repetitive;
        os.flush();
    }
    EXPECT_EQ(large_buf->effective_compression_type(), CompressionType::LZ4);
    delete large_buf;

    std::string large_result = large_oss.str();
    ASSERT_GE(large_result.size(), sizeof(int32_t) * 2);
    int32_t luncomp = 0, lcomp = 0;
    std::memcpy(&luncomp, large_result.data(), sizeof(int32_t));
    std::memcpy(&lcomp, large_result.data() + sizeof(int32_t), sizeof(int32_t));
    EXPECT_EQ(luncomp, 5000);
    EXPECT_LT(lcomp, 5000);  // LZ4 shrinks highly repetitive input
}

// A payload exactly at the threshold (boundary) is still treated as small and
// skipped; one byte over the threshold triggers real compression.
TEST(CompressingStreamBufTest, CompressionThresholdBoundary) {
    constexpr int64_t kThreshold = 128;

    // Exactly at threshold → skip.
    std::ostringstream at_oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        CompressingStreamBuf buf(at_oss, std::move(compressor), 4194304, kThreshold);
        std::ostream os(&buf);
        std::string payload(static_cast<size_t>(kThreshold), 'A');
        os << payload;
        os.flush();
        EXPECT_EQ(buf.effective_compression_type(), CompressionType::NONE);
    }

    // One byte over → compress.
    std::ostringstream over_oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        CompressingStreamBuf buf(over_oss, std::move(compressor), 4194304, kThreshold);
        std::ostream os(&buf);
        std::string payload(static_cast<size_t>(kThreshold + 1), 'A');
        os << payload;
        os.flush();
        EXPECT_EQ(buf.effective_compression_type(), CompressionType::LZ4);
    }
}

// Regression guard: when compression_threshold >= chunk_size, the skip fast
// path MUST stay disabled. Otherwise a payload larger than chunk_size would
// emit a mix of "first raw chunk + later compressed chunks", which the
// read-side DecompressingStreamBuf cannot decode (it keys off a single
// header.compression_type_). With threshold >= chunk_size we must compress
// every chunk as configured.
TEST(CompressingStreamBufTest, NoSkipWhenThresholdExceedsChunkSize) {
    // chunk_size = 64, threshold = 4096 (>= chunk_size). Even though the first
    // chunk's 64 bytes are <= threshold, skip is forbidden — effective type
    // must stay LZ4 and multi-chunk output must remain decodable.
    std::ostringstream oss;
    {
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        CompressingStreamBuf buf(oss, std::move(compressor), 64, 4096);
        std::ostream os(&buf);
        std::string payload(500, 'X');  // spans many 64-byte chunks
        os << payload;
        os.flush();
        EXPECT_EQ(buf.effective_compression_type(), CompressionType::LZ4);
        EXPECT_GT(buf.chunk_count(), 1);
    }
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

TEST(FlyBufferStreamBufTest, WritesData) {
    FlyBuffer buf;
    FlyBufferStreamBuf sbuf(buf);
    std::ostream os(&sbuf);
    os << "hello world";
    os.flush();
    ASSERT_EQ(buf.size(), 11u);
    EXPECT_EQ(std::string(buf.data(), buf.size()), "hello world");
}

TEST(FlyBufferStreamBufTest, LargeData) {
    FlyBuffer buf;
    FlyBufferStreamBuf sbuf(buf);
    std::ostream os(&sbuf);
    std::string large_data(10000, 'X');
    os << large_data;
    os.flush();
    EXPECT_EQ(buf.size(), 10000u);
    EXPECT_EQ(std::string(buf.data(), 10000), large_data);
}

TEST(CountingStreamBufTest, CountsBytes) {
    FlyBuffer buf;
    FlyBufferStreamBuf inner(buf);
    CountingStreamBuf counter(inner);
    std::ostream os(&counter);
    os << "12345";
    os.flush();
    EXPECT_EQ(counter.bytes_written(), 5);
    EXPECT_EQ(std::string(buf.data(), buf.size()), "12345");
}

TEST(CountingStreamBufTest, MultipleWrites) {
    FlyBuffer buf;
    FlyBufferStreamBuf inner(buf);
    CountingStreamBuf counter(inner);
    std::ostream os(&counter);
    os << "ab";
    os << "cd";
    os << "ef";
    os.flush();
    EXPECT_EQ(counter.bytes_written(), 6);
    EXPECT_EQ(std::string(buf.data(), buf.size()), "abcdef");
}

}  // namespace fly