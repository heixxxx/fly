#pragma once

#include <cstdint>

// 压缩类型 —— 存储与网络共用的稳定 wire 值（自 storage/cpp/compressor.h
// 下沉：落盘对象头 ObjectHeader.compression_type_ 与线上消息
// DataResponseMessage.chunk_compression_type_ /
// PeerStreamStartMessage.compression_type_ 都承载本枚举，而 message_types
// 与 object_header 不允许反向依赖 storage——分层收敛）。
//
// 字节级红线：盘上（对象头 fixed 段/trailer 各 1 字节）与线上恒 1 字节
// 0-3 直通。底层类型自 int8_t 定型为 uint8_t——0-3 的补码表示相同，编码
// 不变；消费侧越界值不再落入未定义枚举值，由 is_valid_compression_type
// 确定性拒绝（对齐 is_valid_message_type 先例）。
enum class CompressionType : uint8_t {
    NONE = 0,
    LZ4 = 1,
    ZLIB = 2,
    ZSTD = 3,
};

// wire/盘面值域校验：越界值 = 数据损坏 / 协议错位，调用方必须按零容忍
// 语义拒绝，不得静默 static_cast 成未定义枚举值继续消费。
inline bool is_valid_compression_type(uint8_t raw) {
    return raw <= static_cast<uint8_t>(CompressionType::ZSTD);
}
