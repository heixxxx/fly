#pragma once

#include <serialization/cpp/fly_buffer.h>
#include <common/cpp/common_types.h>
#include <streambuf>
#include <cstdint>

class FlyBufferStreamBuf : public std::streambuf {
public:
    explicit FlyBufferStreamBuf(FlyBuffer& buf) : buf_(buf) {}

protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            buf_.write(reinterpret_cast<const char*>(&ch), 1);
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        buf_.write(s, static_cast<size_t>(n));
        return n;
    }

private:
    FlyBuffer& buf_;
};

class CountingStreamBuf : public std::streambuf {
public:
    explicit CountingStreamBuf(std::streambuf& inner) : inner_(inner) {}

    int64_t bytes_written() const { return bytes_written_; }

protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) {
            int_type result = inner_.sputc(static_cast<char>(ch));
            if (result != traits_type::eof()) {
                ++bytes_written_;
            }
            return result;
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::streamsize written = inner_.sputn(s, n);
        bytes_written_ += written;
        return written;
    }

private:
    std::streambuf& inner_;
    int64_t bytes_written_ = 0;
};
