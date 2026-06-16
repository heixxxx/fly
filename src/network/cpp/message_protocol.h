#pragma once

#include <network/cpp/connection_manager.h>
#include <network/cpp/message_types.h>
#include <serialization/cpp/fly_buffer.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace fly {

class MessageProtocol {
public:
    template<typename T>
    static CMString encode(const T& msg) {
        CMString payload;
        FLY_ENCODE(msg, payload);
        
        uint32_t total_len = static_cast<uint32_t>(1 + payload.size());
        CMString frame;
        frame.resize(4 + 1 + payload.size());
        
        frame[0] = static_cast<char>((total_len >> 24) & 0xFF);
        frame[1] = static_cast<char>((total_len >> 16) & 0xFF);
        frame[2] = static_cast<char>((total_len >> 8) & 0xFF);
        frame[3] = static_cast<char>(total_len & 0xFF);
        
        frame[4] = static_cast<char>(static_cast<uint8_t>(T::msg_type_));
        
        std::copy(payload.begin(), payload.end(), frame.begin() + 5);
        return frame;
    }
    
    template<typename T>
    static bool decode(CMString& buffer, T& msg) {
        if (buffer.size() < 5) return false;
        
        uint32_t total_len = 
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(buffer[3]));
        
        if (total_len < 1) return false;
        if (buffer.size() < 4 + total_len) return false;
        
        uint8_t raw_type = static_cast<uint8_t>(buffer[4]);
        if (!is_valid_message_type(raw_type)) return false;
        
        MessageType msg_type = static_cast<MessageType>(raw_type);
        if (msg_type != T::msg_type_) return false;
        
        uint32_t payload_len = total_len - 1;
        CMString payload(buffer.substr(5, payload_len));
        
        try {
            FLY_DECODE(payload, T, msg);
        } catch (const std::runtime_error&) {
            return false;
        }
        
        buffer.erase(0, 4 + total_len);
        return true;
    }
    
    static MessageType get_type(const CMString& buffer) {
        if (buffer.size() < 5) return MessageType::REGISTER;
        
        uint32_t total_len = 
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(buffer[3]));
        
        if (total_len < 1) return MessageType::REGISTER;
        if (buffer.size() < 4 + total_len) return MessageType::REGISTER;
        
        uint8_t raw_type = static_cast<uint8_t>(buffer[4]);
        if (!is_valid_message_type(raw_type)) return MessageType::REGISTER;
        
        return static_cast<MessageType>(raw_type);
    }
    
    static uint32_t get_total_size(const CMString& buffer) {
        if (buffer.size() < 4) return 0;
        
        return 
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(buffer[3]));
    }
    
    static uint32_t get_payload_size(const CMString& buffer) {
        uint32_t total_len = get_total_size(buffer);
        return total_len > 0 ? total_len - 1 : 0;
    }
    
    static bool decode_header(const CMString& buffer, MessageHeader& header) {
        if (buffer.size() < 5) return false;
        
        header.type_ = static_cast<MessageType>(static_cast<uint8_t>(buffer[4]));
        header.message_id_ = 0;
        header.timestamp_ = 0;
        return true;
    }
};

// Two-segment protocol for DataResponseMessage: small fields via bitsery, large
// compressed payload as raw bytes. Eliminates user-space copies of the payload.
//
// Wire layout (DATA_RESPONSE only):
//   [4B total_len BE][1B type=DATA_RESPONSE]
//   [4B small_fields_len BE][1B has_raw]
//   [small_fields_len bytes: bitsery-encoded DataResponseMessage (no compressed_data_)]
//   [if has_raw: raw_len bytes of compressed payload]
//
// total_len = 1(type) + 4(small_fields_len) + 1(has_raw) + small_fields_len + raw_len
// raw_len is inferred: total_len - 6 - small_fields_len
class DataResponseProtocol {
public:
    // Server side: build the small-field segment (frame header + sub-header +
    // bitsery-encoded message). The raw payload is referenced by pointer (no copy).
    // Returns {header_segment, raw_ptr, raw_len}. raw_ptr is nullptr if !has_raw.
    struct TwoSegment {
        CMString header_segment;   // [5B frame][5B sub-header][bitsery small fields]
        const char* raw_ptr = nullptr;
        size_t raw_len = 0;
    };

    static TwoSegment encode(const DataResponseMessage& msg, const FlyBufferPtr& raw_data) {
        TwoSegment result;
        CMString small_payload;
        FLY_ENCODE(msg, small_payload);

        bool has_raw = raw_data && !raw_data->empty();
        uint32_t raw_len = has_raw ? static_cast<uint32_t>(raw_data->size()) : 0;
        uint32_t small_fields_len = static_cast<uint32_t>(small_payload.size());
        // total_len = 1(type) + 4(small_fields_len) + 1(has_raw) + small_fields_len + raw_len
        uint32_t total_len = 1 + 4 + 1 + small_fields_len + raw_len;

        result.header_segment.resize(4 + 1 + 4 + 1 + small_fields_len);
        char* p = &result.header_segment[0];
        auto write_be32 = [&](uint32_t v) {
            *p++ = static_cast<char>((v >> 24) & 0xFF);
            *p++ = static_cast<char>((v >> 16) & 0xFF);
            *p++ = static_cast<char>((v >> 8) & 0xFF);
            *p++ = static_cast<char>(v & 0xFF);
        };
        write_be32(total_len);
        *p++ = static_cast<char>(static_cast<uint8_t>(MessageType::DATA_RESPONSE));
        write_be32(small_fields_len);
        *p++ = has_raw ? 1 : 0;
        std::memcpy(p, small_payload.data(), small_fields_len);

        if (has_raw) {
            result.raw_ptr = raw_data->data();
            result.raw_len = raw_len;
        }
        return result;
    }

    // Parse the sub-header (small_fields_len + has_raw) from a 5-byte buffer.
    // Call after reading the first 10 bytes (5 frame header + 5 sub-header).
    static void parse_sub_header(const char* sub_header,
                                  uint32_t& small_fields_len, bool& has_raw) {
        small_fields_len =
            (static_cast<uint32_t>(static_cast<unsigned char>(sub_header[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(sub_header[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(sub_header[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(sub_header[3]));
        has_raw = sub_header[4] != 0;
    }

    // Compute raw payload length from frame total_len and small_fields_len.
    static uint32_t raw_len_from_total(uint32_t total_len, uint32_t small_fields_len) {
        // total_len = 1 + 4 + 1 + small_fields_len + raw_len
        uint32_t overhead = 1 + 4 + 1 + small_fields_len;
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

}  // namespace fly
