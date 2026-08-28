#pragma once

#include <cstddef>
#include <cstdint>

namespace fly {

// ── 数据校验稳定包装层（chunked-transfer-design.md §4.3）─────────────────
//
// 用户裁定：接口一经定稿不再变化——后续若出现更优校验方案，只改
// data_checksum.cpp 的实现，所有调用方零改动。头文件注释锚定契约，
// common/tests/data_checksum_test.cpp 契约测试锚定行为。
//
// 契约（任何实现必须满足）：
//   ① 摘要是 64 位无符号整数；
//   ② 增量 == 整块：DataChecksum 按任意切分 update 后 final()，
//      等于 data_checksum() 对同字节序列一次计算的结果（链式 seed 语义）；
//   ③ 确定性：相同字节序列恒得相同摘要；空输入的 final() 是固定初值；
//   ④ 抗损坏：随机输入下单比特翻转改变摘要的检测强度 ≥ 2^-32 误放过
//      （当前实现 CRC-64 误放过 ≈ 2^-64，远高于契约下限）。
//
// 当前实现：ISA-L crc64_ecma_refl（PCLMUL 多态分派，实测 14.6 GB/s）。
// <isa-l/crc64.h> 只允许出现在 data_checksum.cpp 一处。

// 整块摘要。
uint64_t data_checksum(const char* data, size_t len);

// 增量摘要（== 整块任意切分）。非线程安全（单实例单线程使用）。
class DataChecksum {
public:
    DataChecksum() = default;

    // 链入一段字节。len==0 时状态不变（空段等价）。
    void update(const char* data, size_t len);

    // 结算当前状态。可重复调用（同状态同值）。
    uint64_t final() const;

private:
    uint64_t state_ = 0;  // ISA-L init=0 链式 seed 语义：状态即累计 CRC
};

}  // namespace fly
