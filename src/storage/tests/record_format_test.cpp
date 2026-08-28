// 磁盘 record 新格式测试（chunked-transfer-design.md §4.4 / 测试 8-9）。
//
// 格式（trailer 化 + 块 CRC）：
//   record = [Chunk1..N][trailer_header][u64 trailer_crc]
//   每块:    [i32 unc][i32 comp][u64 crc][data]   crc = data_checksum(块字节)
//   trailer_header = ObjectHeader::serialize()（fixed 20B + py_name）
//   trailer_crc = data_checksum(trailer_header)
//
// trailer 尾置 = 纯追加写 + commit marker（崩溃残块无 trailer 结构上不可误读）。
// idx 零变更（offset+size 即起止区间）；读侧从区间尾部解析 trailer，块流必须
// 恰好消耗 size − trailer 全长。校验失败经 checksum_failed() 显式上报（替换
// 现状静默 EOF 截断）。
#include <gtest/gtest.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/compressor.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/data_checksum.h>

#include <cstring>
#include <string>

namespace fly {
namespace {

// 组装一个完整 record：块流 + trailer（测试侧独立实现线格式，不复用生产
// 组装路径——但块流由 CompressingStreamBuf 产出）。布局 [py_name][fixed][crc]。
CMString make_trailer_bytes(const CMString& py_name, uint64_t total, uint32_t chunks,
                            uint8_t comp_type) {
    const size_t fixed = static_cast<size_t>(ObjectHeader::fixed_header_size());
    CMString body;
    body.append(py_name.data(), py_name.size());
    const uint32_t magic = FLY_OBJECT_MAGIC;
    const uint8_t version = FLY_OBJECT_VERSION;
    const uint16_t nl = static_cast<uint16_t>(py_name.size());
    body.append(reinterpret_cast<const char*>(&magic), 4);
    body.append(reinterpret_cast<const char*>(&version), 1);
    body.append(reinterpret_cast<const char*>(&nl), 2);
    body.append(reinterpret_cast<const char*>(&total), 8);
    body.append(reinterpret_cast<const char*>(&chunks), 4);
    body.append(reinterpret_cast<const char*>(&comp_type), 1);
    uint64_t crc = data_checksum(body.data(), body.size());
    CMString out = body;
    out.append(reinterpret_cast<const char*>(&crc), sizeof(crc));
    return out;
}

// 独立解析 trailer（尾部 8B CRC + 其前 fixed 20B + 紧贴其前的 py_name）。
bool parse_trailer_independent(const CMString& record, ObjectHeader& out, uint64_t& chunk_area) {
    size_t sz = record.size();
    size_t fixed = static_cast<size_t>(ObjectHeader::fixed_header_size());
    if (sz < fixed + sizeof(uint64_t)) return false;

    const char* p = record.data() + sz - sizeof(uint64_t);
    uint64_t crc;
    std::memcpy(&crc, p, sizeof(crc));

    const char* hp = p - fixed;  // fixed 部分
    uint32_t magic;
    uint8_t version;
    uint16_t py_name_len;
    std::memcpy(&magic, hp, 4);
    std::memcpy(&version, hp + 4, 1);
    std::memcpy(&py_name_len, hp + 4 + 1, 2);

    if (magic != FLY_OBJECT_MAGIC) return false;
    if (version > FLY_OBJECT_VERSION) return false;
    // CRC 覆盖 [py_name 起点, fixed 结束) 连续段。
    if (crc != data_checksum(hp - py_name_len, fixed + py_name_len)) return false;

    std::memcpy(&out.total_size_, hp + 4 + 1 + 2, 8);
    std::memcpy(&out.chunk_count_, hp + 4 + 1 + 2 + 8, 4);
    std::memcpy(&out.compression_type_, hp + 4 + 1 + 2 + 8 + 4, 1);
    out.py_name_len_ = py_name_len;
    out.py_name_.assign(hp - py_name_len, py_name_len);

    size_t trailer_len = fixed + py_name_len + sizeof(uint64_t);
    if (sz < trailer_len) return false;
    chunk_area = sz - trailer_len;
    return true;
}

std::string make_payload(size_t n) {
    std::string s(n, '\0');
    for (size_t i = 0; i < n; ++i) s[i] = static_cast<char>((i * 31 + 7) & 0xFF);
    return s;
}

}  // namespace

// 测试 8：块流 + trailer 落盘格式；尾部解析；roundtrip 一致；
// raw passthrough 小对象同格式；块头 CRC 与独立计算一致。
TEST(RecordFormatTest, TrailerRoundtrip) {
    // 多块压缩 record：chunk_size=64 强制多块。
    FlyBuffer buf;
    {
        FlyBufferStreamBuf fb(buf);
        std::ostream fb_os(&fb);
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        CompressingStreamBuf csbuf(fb_os, std::move(compressor), 64, 0 /*force compress*/);
        std::ostream os(&csbuf);
        std::string payload = make_payload(1000);
        os.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        os.flush();
        ASSERT_GT(csbuf.chunk_count(), 1);

        // trailer 由完成方追加（FlyStream/Database 在 L0-3 后续接入）。
        CMString trailer = make_trailer_bytes("builtins.dict", 1000,
                                              static_cast<uint32_t>(csbuf.chunk_count()),
                                              static_cast<uint8_t>(CompressionType::LZ4));
        buf.write(trailer.data(), trailer.size());
    }

    ObjectHeader hdr;
    uint64_t chunk_area = 0;
    ASSERT_TRUE(parse_trailer_independent({buf.data(), buf.size()}, hdr, chunk_area));
    EXPECT_EQ(hdr.py_name_, "builtins.dict");
    EXPECT_EQ(hdr.total_size_, 1000u);
    EXPECT_GT(hdr.chunk_count_, 1u);

    // 块头布局独立验证：[i32 unc][i32 comp][u64 crc]，crc == data_checksum(块字节)。
    {
        const char* p = buf.data();
        int32_t unc, comp;
        std::memcpy(&unc, p, 4);
        std::memcpy(&comp, p + 4, 4);
        uint64_t crc;
        std::memcpy(&crc, p + 8, 8);
        EXPECT_EQ(unc, 64);  // chunk_size=64：满块 unc == 64
        EXPECT_GT(comp, 0);  // 压缩后长度（高熵数据可能膨胀 > unc）
        EXPECT_EQ(crc, data_checksum(p + 16, static_cast<size_t>(comp)));
    }

    // 读侧 roundtrip：DecompressingStreamBuf 尾部解析 + 全量解压一致。
    DecompressingStreamBuf dsbuf(buf.data(), buf.size());
    EXPECT_EQ(dsbuf.py_name(), "builtins.dict");
    std::istream is(&dsbuf);
    std::string got(1000, '\0');
    is.read(got.data(), 1000);
    EXPECT_EQ(is.gcount(), static_cast<std::streamsize>(1000));
    EXPECT_EQ(got, make_payload(1000));
    EXPECT_FALSE(dsbuf.checksum_failed());
}

// 测试 8b：raw passthrough 小对象（skipped 分支）同格式同校验。
TEST(RecordFormatTest, RawPassthroughSameFormat) {
    FlyBuffer buf;
    {
        FlyBufferStreamBuf fb(buf);
        std::ostream fb_os(&fb);
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        CompressingStreamBuf csbuf(fb_os, std::move(compressor), 4194304, 4096);
        std::ostream os(&csbuf);
        std::string payload = "tiny payload";  // <= threshold → raw passthrough
        os.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        os.flush();
        ASSERT_EQ(csbuf.chunk_count(), 1);
        CMString trailer = make_trailer_bytes("str", payload.size(), 1,
                                              static_cast<uint8_t>(CompressionType::NONE));
        buf.write(trailer.data(), trailer.size());
    }

    ObjectHeader hdr;
    uint64_t chunk_area = 0;
    ASSERT_TRUE(parse_trailer_independent({buf.data(), buf.size()}, hdr, chunk_area));
    EXPECT_EQ(hdr.py_name_, "str");
    EXPECT_EQ(hdr.total_size_, 12u);

    DecompressingStreamBuf dsbuf(buf.data(), buf.size());
    std::istream is(&dsbuf);
    std::string got(12, '\0');
    is.read(got.data(), 12);
    EXPECT_EQ(got, "tiny payload");
    EXPECT_FALSE(dsbuf.checksum_failed());
}

// 测试 9：损坏检测——块数据翻转 1 字节 / CRC 域翻转 / trailer 损坏 / 截断残块，
// 全部 checksum_failed() == true（非静默 EOF 截断）。
TEST(RecordFormatTest, CorruptChunkDetected) {
    auto build = [](FlyBuffer& buf) {
        FlyBufferStreamBuf fb(buf);
        std::ostream fb_os(&fb);
        auto compressor = CompressorFactory::create(CompressionType::LZ4);
        CompressingStreamBuf csbuf(fb_os, std::move(compressor), 64, 0);
        std::ostream os(&csbuf);
        std::string payload = make_payload(500);
        os.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        os.flush();
        CMString trailer = make_trailer_bytes("x", 500,
                                              static_cast<uint32_t>(csbuf.chunk_count()),
                                              static_cast<uint8_t>(CompressionType::LZ4));
        buf.write(trailer.data(), trailer.size());
    };

    // 基线：完好 record 读全量成功。
    {
        FlyBuffer good;
        build(good);
        DecompressingStreamBuf dsbuf(good.data(), good.size());
        std::istream is(&dsbuf);
        std::string got(500, '\0');
        is.read(got.data(), 500);
        EXPECT_EQ(is.gcount(), static_cast<std::streamsize>(500));
        EXPECT_FALSE(dsbuf.checksum_failed());
    }

    // a) 第一块数据域翻转 1 字节 → 块 CRC 失配。
    {
        FlyBuffer bad;
        build(bad);
        char* p = bad.data();
        p[16] ^= 0x01;  // 块头 16B 之后是数据
        DecompressingStreamBuf dsbuf(bad.data(), bad.size());
        std::istream is(&dsbuf);
        std::string got(500, '\0');
        is.read(got.data(), 500);
        EXPECT_TRUE(dsbuf.checksum_failed()) << "flipped block data not detected";
    }

    // b) CRC 域翻转 1 字节 → 失配。
    {
        FlyBuffer bad;
        build(bad);
        char* p = bad.data();
        p[8] ^= 0x80;
        DecompressingStreamBuf dsbuf(bad.data(), bad.size());
        std::istream is(&dsbuf);
        std::string got(500, '\0');
        is.read(got.data(), 500);
        EXPECT_TRUE(dsbuf.checksum_failed()) << "flipped crc field not detected";
    }

    // c) trailer CRC 域翻转 → trailer 解析失败。
    {
        FlyBuffer bad;
        build(bad);
        char* p = bad.data() + bad.size();
        p[-1] ^= 0x01;
        DecompressingStreamBuf dsbuf(bad.data(), bad.size());
        std::istream is(&dsbuf);
        std::string got(64, '\0');
        is.read(got.data(), 64);
        EXPECT_TRUE(dsbuf.checksum_failed()) << "corrupted trailer not detected";
    }

    // d) 截断（去掉末块 + trailer 重算）：残块流 + 合法 trailer → 块流越界。
    //    模拟"写一半崩溃 + 无 trailer"的真实形态：直接截掉尾部。
    {
        FlyBuffer bad;
        build(bad);
        FlyBuffer truncated;
        truncated.write(bad.data(), bad.size() - 80);  // 截掉 trailer + 部分末块
        DecompressingStreamBuf dsbuf(truncated.data(), truncated.size());
        std::istream is(&dsbuf);
        std::string got(500, '\0');
        is.read(got.data(), 500);
        EXPECT_TRUE(dsbuf.checksum_failed()) << "truncated record not detected";
    }
}

// 旧格式（header 前置、块头 8B、无 CRC）record：新读侧必须显式拒绝
// （checksum_failed，非静默空流）——用户裁定不做版本兼容，重建 db。
TEST(RecordFormatTest, LegacyPrefixHeaderRejected) {
    // 旧格式：[ObjectHeader][8B 块头 + 数据]
    ObjectHeader hdr;
    hdr.py_name_ = "legacy";
    hdr.py_name_len_ = 6;
    hdr.total_size_ = 16;
    hdr.chunk_count_ = 1;
    hdr.compression_type_ = static_cast<uint8_t>(CompressionType::NONE);
    CMString record = hdr.serialize();
    int32_t sz = 16;
    record.append(reinterpret_cast<const char*>(&sz), 4);
    record.append(reinterpret_cast<const char*>(&sz), 4);
    record.append(std::string(16, 'L'));

    DecompressingStreamBuf dsbuf(record.data(), record.size());
    EXPECT_TRUE(dsbuf.checksum_failed()) << "legacy format must be rejected explicitly";
}

}  // namespace fly
