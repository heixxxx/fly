#pragma once

#include <network/cpp/transport.h>
#include <network/cpp/message_types.h>
#include <cstdint>

namespace fly {

class MessageProtocol {
public:
    template<typename T>
    static CMString encode(const T& msg) {
        CMString payload;
        FLY_ENCODE(msg, payload);
        
        uint32_t len = static_cast<uint32_t>(payload.size());
        CMString frame;
        frame.resize(4 + payload.size());
        
        frame[0] = static_cast<char>((len >> 24) & 0xFF);
        frame[1] = static_cast<char>((len >> 16) & 0xFF);
        frame[2] = static_cast<char>((len >> 8) & 0xFF);
        frame[3] = static_cast<char>(len & 0xFF);
        
        std::copy(payload.begin(), payload.end(), frame.begin() + 4);
        return frame;
    }
    
    template<typename T>
    static bool decode(CMString& buffer, T& msg) {
        if (buffer.size() < 4) return false;
        
        uint32_t len = 
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(buffer[3]));
        
        if (buffer.size() < 4 + len) return false;
        
        CMString payload(buffer.substr(4, len));
        buffer.erase(0, 4 + len);
        
        FLY_DECODE(payload, T, msg);
        return true;
    }
    
    static MessageType get_type(const CMString& buffer) {
        if (buffer.size() < 4) return MessageType::REGISTER;
        
        uint32_t len = 
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(buffer[3]));
        
        if (buffer.size() < 4 + len) return MessageType::REGISTER;
        
        MessageHeader result;
        CMString temp = buffer.substr(4, len);
        FLY_DECODE(temp, MessageHeader, result);
        return result.type;
    }
    
    static uint32_t get_payload_size(const CMString& buffer) {
        if (buffer.size() < 4) return 0;
        
        return 
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[0])) << 24) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[1])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(buffer[2])) << 8) |
            static_cast<uint32_t>(static_cast<unsigned char>(buffer[3]));
    }
};

}  // namespace fly