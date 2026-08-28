#include <common/cpp/data_checksum.h>

// <isa-l/crc64.h> 全仓唯一包含点：更换校验方案时只改本文件（契约见
// data_checksum.h 头注释 + common/tests/data_checksum_test.cpp）。
#include <isa-l/crc64.h>

namespace fly {

uint64_t data_checksum(const char* data, size_t len) {
    // crc64_ecma_refl 的 init=0 链式 seed 语义：对完整序列一次计算即摘要；
    // 分段调用时上一段的输出作下一段 seed —— DataChecksum::update 依赖此性质。
    return crc64_ecma_refl(0, reinterpret_cast<const unsigned char*>(data), len);
}

void DataChecksum::update(const char* data, size_t len) {
    state_ = crc64_ecma_refl(state_, reinterpret_cast<const unsigned char*>(data), len);
}

uint64_t DataChecksum::final() const { return state_; }

}  // namespace fly
