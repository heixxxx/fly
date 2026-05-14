#pragma once

#include <zpp_bits.h>
#include <string>
#include <vector>
#include <cstddef>
#include <algorithm>

#define FLY_SERIALIZE_DECLARE() \
    constexpr static auto serialize(auto& archive)

#define FLY_SERIALIZE_FIELDS(...) archive(__VA_ARGS__);

#define FLY_SERIALIZE_BASE(base_class) archive(base_class::serialize(archive));

#define FLY_ENCODE(msg, output) \
    do { \
        auto [data, out] = zpp::bits::data_out(); \
        out(msg).or_throw(); \
        output.resize(data.size()); \
        std::transform(data.begin(), data.end(), output.begin(), \
            [](std::byte b) { return static_cast<char>(b); }); \
    } while(0)

#define FLY_DECODE(data, msg_type, output) \
    do { \
        std::vector<std::byte> buf(data.size()); \
        std::transform(data.begin(), data.end(), buf.begin(), \
            [](char c) { return static_cast<std::byte>(c); }); \
        auto in = zpp::bits::in(buf); \
        msg_type msg; \
        in(msg).or_throw(); \
        output = std::move(msg); \
    } while(0)

#define FLY_ENCODE_TO_BYTES(msg, output) \
    do { \
        auto [data, out] = zpp::bits::data_out(); \
        out(msg).or_throw(); \
        output.resize(data.size()); \
        std::transform(data.begin(), data.end(), output.begin(), \
            [](std::byte b) { return static_cast<unsigned char>(b); }); \
    } while(0)

#define FLY_DECODE_FROM_BYTES(buf, msg_type, output) \
    do { \
        std::vector<std::byte> byte_buf(buf.size()); \
        std::transform(buf.begin(), buf.end(), byte_buf.begin(), \
            [](unsigned char c) { return static_cast<std::byte>(c); }); \
        auto in = zpp::bits::in(byte_buf); \
        msg_type msg; \
        in(msg).or_throw(); \
        output = std::move(msg); \
    } while(0)
