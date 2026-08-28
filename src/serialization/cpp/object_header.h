#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <string_view>

constexpr uint32_t FLY_OBJECT_MAGIC = 0x464C5900;
constexpr uint8_t FLY_OBJECT_VERSION = 1;

struct ObjectHeader {
    uint32_t magic_ = FLY_OBJECT_MAGIC;
    uint8_t version_ = FLY_OBJECT_VERSION;
    uint16_t py_name_len_ = 0;
    CMString py_name_;
    uint64_t total_size_ = 0;
    uint32_t chunk_count_ = 0;
    uint8_t compression_type_ = 0;

    CMString serialize() const;
    // Zero-copy: accepts std::string_view — CMString, FlyBuffer data, or raw
    // pointer+size all construct a string_view implicitly without copying.
    // 解析失败（数据不足/magic 错/版本超期/py_name 截断）返回 false（out 不被
    // 修改；offset 可能已部分推进，失败时调用方不应再消费 offset——issue 002 批次 C）。
    static bool deserialize(std::string_view data, int64_t& offset, ObjectHeader& out);

    // trailer 序列化（磁盘 record 新格式，chunked-transfer-design.md §4.4）：
    //   [py_name][fixed 20B][u64 crc]  （crc 覆盖其前全部 header 字节）
    // 与 serialize() 的字段顺序不同：py_name 前置使尾部解析可【定长锚定】
    // （fixed 起点 = 末 8B CRC 前 20B），无需按 py_name_len 回扫。
    CMString serialize_trailer() const;

    // 尾部解析（trailer 格式，chunked-transfer-design.md §4.4）：
    //   record = [Chunk1..N][trailer_header][u64 trailer_crc]
    // 从 record 末尾解析 trailer：末 8B 是 crc，其前 20B 是 fixed 部分，
    // py_name 紧贴 fixed 之前（长度在 fixed 内）。
    // 成功时 out 填充且 trailer_len 返回 trailer 全长（含 crc），
    // 块流区域 = record.size() - trailer_len。
    // 解析失败（长度不足/magic 错/版本超期/CRC 失配）返回 false —— 调用方必须
    // 按数据损坏处理（零容忍语义），不得静默降级。
    static bool deserialize_trailer(std::string_view record, ObjectHeader& out,
                                    size_t& trailer_len);

    static constexpr int64_t fixed_header_size() {
        return sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) +
               sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint8_t);
    }

    bool is_valid() const;
};