#pragma once

#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <cstdint>
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
        
        frame[4] = static_cast<char>(static_cast<uint8_t>(T::msg_type));
        
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
        
        if (buffer.size() < 4 + total_len) return false;
        
        MessageType msg_type = static_cast<MessageType>(static_cast<uint8_t>(buffer[4]));
        if (msg_type != T::msg_type) return false;
        
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
        
        if (buffer.size() < 4 + total_len) return MessageType::REGISTER;
        
        return static_cast<MessageType>(static_cast<uint8_t>(buffer[4]));
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
};

}  // namespace fly