#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <string_view>
#include <vector>

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
    // 块位置表（§14.1 B'）：每块压缩后字节长（不含 16B 块头）。紧凑式——
    // 前缀和即得各块偏移。磁盘侧消费者：读侧对账（Σ(comp_len+16) == 块区
    // 总长，防块头域损坏导致的边界漂移）与 L4 部分读。内存/bitsery 路径
    // （serialize/deserialize）不携带此字段。
    CMVector<uint32_t> block_comp_lens_;

    CMString serialize() const;
    // Zero-copy: accepts std::string_view — CMString, FlyBuffer data, or raw
    // pointer+size all construct a string_view implicitly without copying.
    // 解析失败（数据不足/magic 错/版本超期/py_name 截断）返回 false（out 不被
    // 修改；offset 可能已部分推进，失败时调用方不应再消费 offset——issue 002 批次 C）。
    static bool deserialize(std::string_view data, int64_t& offset, ObjectHeader& out);

    // trailer 序列化（磁盘 record v2 格式，chunked-transfer-design.md §4.4/§14.1）：
    //   [块表 N×u32][py_name][fixed 24B][u64 crc]  （crc 覆盖块表+py_name+fixed）
    // 变长域（块表/py_name）的长度全部登记在定长 fixed 内，尾部 32B（fixed+crc）
    // 定长锚定——解析从末尾出发方向唯一。
    CMString serialize_trailer() const;

    // 尾部解析（v2 格式）：末 8B crc → 前 24B fixed（magic/version/py_name_len/
    // block_table_len/total/chunk_count/comp_type）→ 双口径互验
    // （block_table_len == chunk_count×4）→ 反推 py_name/块表。
    // 成功时 out 填充（含 block_comp_lens_）且 trailer_len 返回全长（含块表+crc），
    // 块流区域 = record.size() - trailer_len。
    // 解析失败（长度不足/magic 错/版本超期/口径失配/CRC 失配）返回 false ——
    // 调用方必须按数据损坏处理（零容忍语义），不得静默降级。
    static bool deserialize_trailer(std::string_view record, ObjectHeader& out,
                                    size_t& trailer_len);

    // v2 fixed：magic(4)+version(1)+py_name_len(2)+block_table_len(4)+
    // total_size(8)+chunk_count(4)+compression_type(1) = 24B
    static constexpr int64_t fixed_header_size() {
        return sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint16_t) +
               sizeof(uint32_t) +
               sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint8_t);
    }

    bool is_valid() const;
};