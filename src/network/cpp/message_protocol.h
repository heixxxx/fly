#pragma once

#include <network/cpp/connection_manager.h>
#include <network/cpp/message_types.h>
#include <common/cpp/fly_buffer.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace fly {

// Big-endian 32-bit integer read/write — shared by MessageProtocol and
// DataResponseProtocol frame parsing, and by all data/metadata clients and
// data_server that read frame headers off the wire.
inline uint32_t read_be32(const char* p) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(p[0])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) <<  8) |
            static_cast<uint32_t>(static_cast<unsigned char>(p[3]));
}
inline uint32_t read_be32(const CMString& s, size_t off = 0) {
    return read_be32(s.data() + off);
}
inline void write_be32(char* p, uint32_t v) {
    p[0] = static_cast<char>((v >> 24) & 0xFF);
    p[1] = static_cast<char>((v >> 16) & 0xFF);
    p[2] = static_cast<char>((v >>  8) & 0xFF);
    p[3] = static_cast<char>( v        & 0xFF);
}

inline uint64_t read_be64(const char* p) {
    return (static_cast<uint64_t>(static_cast<unsigned char>(p[0])) << 56) |
           (static_cast<uint64_t>(static_cast<unsigned char>(p[1])) << 48) |
           (static_cast<uint64_t>(static_cast<unsigned char>(p[2])) << 40) |
           (static_cast<uint64_t>(static_cast<unsigned char>(p[3])) << 32) |
           (static_cast<uint64_t>(static_cast<unsigned char>(p[4])) << 24) |
           (static_cast<uint64_t>(static_cast<unsigned char>(p[5])) << 16) |
           (static_cast<uint64_t>(static_cast<unsigned char>(p[6])) <<  8) |
            static_cast<uint64_t>(static_cast<unsigned char>(p[7]));
}
inline void write_be64(char* p, uint64_t v) {
    p[0] = static_cast<char>((v >> 56) & 0xFF);
    p[1] = static_cast<char>((v >> 48) & 0xFF);
    p[2] = static_cast<char>((v >> 40) & 0xFF);
    p[3] = static_cast<char>((v >> 32) & 0xFF);
    p[4] = static_cast<char>((v >> 24) & 0xFF);
    p[5] = static_cast<char>((v >> 16) & 0xFF);
    p[6] = static_cast<char>((v >>  8) & 0xFF);
    p[7] = static_cast<char>( v        & 0xFF);
}

// ── 64 位帧头（chunked-transfer-design.md §4.1）──────────────────────────
//
// 帧前缀 9B：[8B header BE][1B type]
//   header = (check << 48) | len
//   len    = 1 + payload_size     —— 48 位，上限 256TB，消除 uint32 截断
//                                     静默回绕（4GiB）与 client 假上限
//   check  = 0xF17E ^ fold16(len) —— 高 16 位校验域
//   fold16(len) = (len ^ (len >> 16) ^ (len >> 32)) & 0xFFFF
//
// 性质：长度域任一单比特翻转 → fold 确定性变化（每位唯一映射到一个保留
// fold 位，无抵消）→ check 失配被拒；失步/垃圾 8 字节误过概率 2^-16；
// len = 0 非法。测试锚定：message_protocol_test.cpp FrameHeaderTest。
inline constexpr uint64_t FRAME_LEN_MASK = 0x0000FFFFFFFFFFFFULL;  // 48 位长度域
inline constexpr uint64_t FRAME_CHECK_MAGIC = 0xF17EULL;           // 校验域魔数

inline uint64_t fold16(uint64_t len) {
    return (len ^ (len >> 16) ^ (len >> 32)) & 0xFFFF;
}

// total_len（1 + payload）→ 8B header 值。要求 len >= 1 且 <= 48 位。
inline uint64_t make_frame_header(uint64_t total_len) {
    return (FRAME_CHECK_MAGIC ^ fold16(total_len)) << 48 | (total_len & FRAME_LEN_MASK);
}

// 校验并解析 8B header：check 位失配 / len == 0 / len 溢出 48 位 → false。
inline bool parse_frame_header(const char* p8, uint64_t& total_len) {
    uint64_t hdr = read_be64(p8);
    uint64_t len = hdr & FRAME_LEN_MASK;
    uint64_t check = hdr >> 48;
    if (len == 0) return false;
    if (check != ((FRAME_CHECK_MAGIC ^ fold16(len)) & 0xFFFF)) return false;
    total_len = len;
    return true;
}

class MessageProtocol {
public:
    template<typename T>
    static CMString encode(const T& msg) {
        CMString payload;
        FLY_ENCODE(msg, payload);

        uint64_t total_len = 1 + payload.size();
        CMString frame;
        frame.resize(8 + 1 + payload.size());
        write_be64(&frame[0], make_frame_header(total_len));
        frame[8] = static_cast<char>(static_cast<uint8_t>(T::msg_type_));

        std::copy(payload.begin(), payload.end(), frame.begin() + 9);
        return frame;
    }

    template<typename T>
    static bool decode(CMString& buffer, T& msg) {
        if (buffer.size() < 9) return false;

        uint64_t total_len = 0;
        if (!parse_frame_header(buffer.data(), total_len)) return false;
        if (buffer.size() < 8 + total_len) return false;

        uint8_t raw_type = static_cast<uint8_t>(buffer[8]);
        if (!is_valid_message_type(raw_type)) return false;

        MessageType msg_type = static_cast<MessageType>(raw_type);
        if (msg_type != T::msg_type_) return false;

        uint64_t payload_len = total_len - 1;
        CMString payload(buffer.substr(9, payload_len));

        try {
            FLY_DECODE(payload, T, msg);
        } catch (const std::runtime_error&) {
            return false;
        }

        buffer.erase(0, 8 + total_len);
        return true;
    }

    static MessageType get_type(const CMString& buffer) {
        if (buffer.size() < 9) return MessageType::INVALID;

        uint64_t total_len = 0;
        if (!parse_frame_header(buffer.data(), total_len)) return MessageType::INVALID;
        if (buffer.size() < 8 + total_len) return MessageType::INVALID;

        uint8_t raw_type = static_cast<uint8_t>(buffer[8]);
        if (!is_valid_message_type(raw_type)) return MessageType::INVALID;

        return static_cast<MessageType>(raw_type);
    }

    // 帧的 total_len（1 + payload，不含 8B header 前缀）。check 位失配等
    // 非法头返回 0。帧在缓冲中的总字节数 = 8 + total_len。
    static uint64_t get_total_size(const CMString& buffer) {
        if (buffer.size() < 8) return 0;

        uint64_t total_len = 0;
        if (!parse_frame_header(buffer.data(), total_len)) return 0;
        return total_len;
    }

    static uint64_t get_payload_size(const CMString& buffer) {
        uint64_t total_len = get_total_size(buffer);
        return total_len > 0 ? total_len - 1 : 0;
    }
};

// Two-segment protocol for DataResponseMessage: small fields via bitsery, large
// compressed payload as raw bytes. Eliminates user-space copies of the payload.
//
// Wire layout (DATA_RESPONSE only):
//   [8B frame header BE (64-bit, §4.1)][1B type=DATA_RESPONSE]
//   [4B small_fields_len BE][1B has_raw]
//   [small_fields_len bytes: bitsery-encoded DataResponseMessage (no compressed_data_)]
//   [if has_raw: raw_len bytes of compressed payload]
//
// total_len = 1(type) + 4(small_fields_len) + 1(has_raw) + small_fields_len + raw_len
// （全 uint64 运算——raw_len 可达 256TB 帧域内任意值）
// raw_len is inferred: total_len - 6 - small_fields_len
class DataResponseProtocol {
public:
    // Server side: build the small-field segment (frame header + sub-header +
    // bitsery-encoded message). The raw payload is referenced by pointer (no copy).
    // Returns {header_segment, raw_ptr, raw_len}. raw_ptr is nullptr if !has_raw.
    struct TwoSegment {
        CMString header_segment;   // [9B frame][5B sub-header][bitsery small fields]
        const char* raw_ptr = nullptr;
        uint64_t raw_len = 0;
    };

    static TwoSegment encode(const DataResponseMessage& msg, const FlyBufferPtr& raw_data) {
        TwoSegment result;
        CMString small_payload;
        FLY_ENCODE(msg, small_payload);

        bool has_raw = raw_data && !raw_data->empty();
        uint64_t raw_len = has_raw ? raw_data->size() : 0;
        uint64_t small_fields_len = small_payload.size();
        // total_len = 1(type) + 4(small_fields_len) + 1(has_raw) + small_fields_len + raw_len
        uint64_t total_len = 1 + 4 + 1 + small_fields_len + raw_len;

        result.header_segment.resize(8 + 1 + 4 + 1 + small_fields_len);
        char* p = &result.header_segment[0];
        write_be64(p, make_frame_header(total_len)); p += 8;
        *p++ = static_cast<char>(static_cast<uint8_t>(MessageType::DATA_RESPONSE));
        write_be32(p, static_cast<uint32_t>(small_fields_len)); p += 4;
        *p++ = has_raw ? 1 : 0;
        std::memcpy(p, small_payload.data(), small_fields_len);

        if (has_raw) {
            result.raw_ptr = raw_data->data();
            result.raw_len = raw_len;
        }
        return result;
    }

    // Parse the sub-header (small_fields_len + has_raw) from a 5-byte buffer.
    // Call after reading the first 14 bytes (9B frame header + 5B sub-header).
    static void parse_sub_header(const char* sub_header,
                                  uint32_t& small_fields_len, bool& has_raw) {
        small_fields_len = read_be32(sub_header);
        has_raw = sub_header[4] != 0;
    }

    // Compute raw payload length from frame total_len and small_fields_len.
    // 全 uint64：大对象（>4GiB）raw_len 不截断。
    static uint64_t raw_len_from_total(uint64_t total_len, uint64_t small_fields_len) {
        // total_len = 1 + 4 + 1 + small_fields_len + raw_len
        uint64_t overhead = 1 + 4 + 1 + small_fields_len;
        return total_len > overhead ? total_len - overhead : 0;
    }

    // Decode small fields from the received bitsery blob.
    static bool decode_small_fields(const CMString& small_payload, DataResponseMessage& msg) {
        try {
            FLY_DECODE(small_payload, DataResponseMessage, msg);
            return true;
        } catch (const std::runtime_error&) {
            return false;
        }
    }
};

// ── DATA_CHUNK 帧协议（L2 分片传输，chunked-transfer-design.md §4.5）──
//
// 纯字节切片的两段式帧（raw 引用零拷贝，同 DATA_RESPONSE 模式）：
//   [8B frame header][1B type=DATA_CHUNK][4B small_fields_len=12]
//   [u32 seq BE][u64 片CRC BE][raw: 分片字节（默认 4MB 切片）]
// 片内容 = 对象 record 区间 [seq*frame_bytes, (seq+1)*frame_bytes) 的原样
// 字节——client 顺序重组后与磁盘 record 字节一致（DecompressingStreamBuf
// 直接消费，磁盘块 CRC 语义完整保留）。片 CRC = data_checksum(片字节)。
class ChunkFrameProtocol {
public:
    static constexpr uint32_t kSmallFieldsLen = sizeof(uint32_t) + sizeof(uint64_t);  // 12

    // 组帧头段（帧头 + type + 子头 + seq/crc）。与 raw 一起发送（writev/两段 send）。
    static CMString encode_header(uint32_t seq, uint64_t chunk_crc, uint64_t raw_len) {
        uint64_t total_len = 1 + 4 + kSmallFieldsLen + raw_len;
        CMString header;
        header.resize(8 + 1 + 4 + kSmallFieldsLen);
        char* p = &header[0];
        write_be64(p, make_frame_header(total_len)); p += 8;
        *p++ = static_cast<char>(static_cast<uint8_t>(MessageType::DATA_CHUNK));
        write_be32(p, kSmallFieldsLen); p += 4;
        write_be32(p, seq); p += 4;
        write_be64(p, chunk_crc);
        return header;
    }

    // 解析子头（12B：seq + crc）。调用方已读过 9B 帧头与 4B small_fields_len。
    // small_fields_len != 12 → false（协议失步）。
    static bool parse_small_fields(const char* p12, uint32_t small_fields_len,
                                   uint32_t& seq, uint64_t& chunk_crc) {
        if (small_fields_len != kSmallFieldsLen) return false;
        seq = read_be32(p12);
        chunk_crc = read_be64(p12 + 4);
        return true;
    }

    // raw 段长度：total_len - 1(type) - 4(small_len) - 12(small fields)。
    static uint64_t raw_len_from_total(uint64_t total_len) {
        uint64_t overhead = 1 + 4 + kSmallFieldsLen;
        return total_len > overhead ? total_len - overhead : 0;
    }
};

}  // namespace fly
