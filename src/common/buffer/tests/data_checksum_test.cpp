// data_checksum 契约测试（chunked-transfer-design.md §4.3 / 测试 5）。
//
// 这些测试锚定的是【接口契约】而非 ISA-L 的具体行为：未来更换校验实现
// （只改 data_checksum.cpp）时，本文件必须不改而全绿。
//   ① 64 位摘要
//   ② 增量 == 整块（任意切分等价）
//   ③ 确定性：同输入同摘要；空输入 final() 为固定初值
//   ④ 随机损坏检测：单比特/单字节翻转必须改变摘要
#include <gtest/gtest.h>
#include <common/buffer/cpp/data_checksum.h>

#include <random>
#include <vector>

namespace {

std::vector<char> make_pattern(size_t len, uint32_t seed) {
    std::vector<char> buf(len);
    std::mt19937 rng(seed);
    for (size_t i = 0; i < len; ++i) buf[i] = static_cast<char>(rng() & 0xFF);
    return buf;
}

}  // namespace

TEST(DataChecksumContract, IncrementalEqualsWholeArbitrarySplits) {
    // 64MB 随机数据，三种切分：1B×N、1MB×64、不等长随机切分。
    const size_t len = 64ULL << 20;
    auto data = make_pattern(len, 42);
    uint64_t whole = fly::data_checksum(data.data(), len);

    {
        fly::DataChecksum cs;
        cs.update(data.data(), len);
        EXPECT_EQ(cs.final(), whole);
    }
    {  // 1MB 块
        fly::DataChecksum cs;
        for (size_t off = 0; off < len; off += (1 << 20)) {
            size_t n = std::min<size_t>(1 << 20, len - off);
            cs.update(data.data() + off, n);
        }
        EXPECT_EQ(cs.final(), whole);
    }
    {  // 不等长随机切分
        fly::DataChecksum cs;
        std::mt19937 rng(7);
        size_t off = 0;
        while (off < len) {
            size_t n = 1 + (rng() % 90000);
            n = std::min(n, len - off);
            cs.update(data.data() + off, n);
            off += n;
        }
        EXPECT_EQ(cs.final(), whole);
    }
}

TEST(DataChecksumContract, DeterministicEmptyInputFixedInit) {
    // 空输入 final() 是固定初值（幂等：多次调用同值；跨实例同值）。
    uint64_t e1 = fly::DataChecksum{}.final();
    uint64_t e2 = fly::DataChecksum{}.final();
    uint64_t e3 = fly::data_checksum(nullptr, 0);
    EXPECT_EQ(e1, e2);
    EXPECT_EQ(e1, e3);

    // update(0 长度) 不改变状态（空段链式等价）。
    const char dummy = 'x';
    fly::DataChecksum cs;
    cs.update(&dummy, 0);
    cs.update(nullptr, 0);
    EXPECT_EQ(cs.final(), e1);
}

TEST(DataChecksumContract, DifferentInputsDifferentDigests) {
    auto a = make_pattern(4096, 1);
    auto b = make_pattern(4096, 2);
    EXPECT_NE(fly::data_checksum(a.data(), a.size()),
              fly::data_checksum(b.data(), b.size()));

    // 前缀关系：短输入的摘要 != 长输入的摘要（防退化实现如"只看长度"）。
    EXPECT_NE(fly::data_checksum(a.data(), 100), fly::data_checksum(a.data(), 101));
    EXPECT_NE(fly::data_checksum(a.data(), 100), fly::data_checksum(a.data(), 200));
}

TEST(DataChecksumContract, SingleBitFlipChangesDigest) {
    auto data = make_pattern(1 << 20, 99);
    uint64_t base = fly::data_checksum(data.data(), data.size());

    // 逐个翻转前 64 字节中每个 bit 位：每次摘要都必须变化。
    for (size_t byte_i = 0; byte_i < 8; ++byte_i) {
        for (int bit = 0; bit < 8; ++bit) {
            auto copy = data;
            copy[byte_i] = static_cast<char>(copy[byte_i] ^ (1 << bit));
            EXPECT_NE(fly::data_checksum(copy.data(), copy.size()), base)
                << "byte=" << byte_i << " bit=" << bit;
        }
    }

    // 尾部单字节翻转同样必须变化。
    auto tail = data;
    tail[tail.size() - 1] ^= 0x01;
    EXPECT_NE(fly::data_checksum(tail.data(), tail.size()), base);
}

TEST(DataChecksumContract, IncrementalAcrossObjectLifetime) {
    // 增量对象可以分多次 update，中间穿插其他操作（各自独立状态）。
    auto data = make_pattern(3 << 20, 5);
    fly::DataChecksum a, b;
    a.update(data.data(), 1 << 20);
    b.update(make_pattern(64, 123).data(), 64);
    a.update(data.data() + (1 << 20), 2 << 20);
    EXPECT_EQ(a.final(), fly::data_checksum(data.data(), data.size()));
    EXPECT_EQ(b.final(), fly::data_checksum(make_pattern(64, 123).data(), 64));
}
