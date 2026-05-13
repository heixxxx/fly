#pragma once

#include <zpp_bits.h>
#include <string>
#include <vector>

// FLY_SERIALIZE_DECLARE - declare serializable type using zpp_bits
#define FLY_SERIALIZE_DECLARE() \
    constexpr static auto serialize(auto& archive)

// FLY_SERIALIZE_FIELDS - serialize multiple fields
#define FLY_SERIALIZE_FIELDS(...) archive(__VA_ARGS__);

// FLY_ENCODE - encode message to string
#define FLY_ENCODE(msg, output) \
    do { \
        auto [data, out] = zpp::bits::data_out(); \
        out(msg).or_throw(); \
        output = std::string(data.begin(), data.end()); \
    } while(0)

// FLY_DECODE - decode message from string
#define FLY_DECODE(data, msg_type, output) \
    do { \
        std::vector<unsigned char> buf(data.begin(), data.end()); \
        auto in = zpp::bits::in(buf); \
        msg_type msg; \
        in(msg).or_throw(); \
        output = std::move(msg); \
    } while(0)