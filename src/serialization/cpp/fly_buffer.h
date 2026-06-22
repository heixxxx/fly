#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <cstring>
#include <iterator>

class FlyBuffer {
public:
    using value_type = char;
    using iterator = CMString::iterator;
    using const_iterator = CMString::const_iterator;
    using difference_type = std::ptrdiff_t;

    FlyBuffer() = default;
    explicit FlyBuffer(size_t capacity) { data_.reserve(capacity); }

    iterator begin() { return data_.begin(); }
    iterator end() { return data_.end(); }
    const_iterator begin() const { return data_.begin(); }
    const_iterator end() const { return data_.end(); }

    void resize(size_t n) { data_.resize(n); }

    void write(const char* data, size_t size) {
        data_.append(data, size);
    }

    // ---- File-protocol read interface (for pickle.load(flybuffer)) ----
    // A read cursor advances through the buffer. read() returns a CMString
    // view (a copy of the requested span — unavoidable since the result must
    // outlive the buffer and cross the Python boundary).
    CMString read(size_t n) {
        size_t avail = data_.size() - pos_;
        size_t take = std::min(n, avail);
        CMString out(data_.data() + pos_, take);
        pos_ += take;
        return out;
    }

    // readline() reads up to and including the next '\n', or to EOF.
    CMString readline() {
        size_t start = pos_;
        size_t nl = data_.find('\n', start);
        size_t end = (nl == CMString::npos) ? data_.size() : nl + 1;
        CMString out(data_.data() + start, end - start);
        pos_ = end;
        return out;
    }

    // readinto(dst, dst_size): writes up to dst_size bytes into the external
    // buffer dst (starting from the cursor), returns the count written.
    // Used by pickle.load via the file protocol so pickle's own working buffer
    // is filled directly (one serialization-inherent copy, no intermediate
    // Python bytes object).
    size_t readinto(char* dst, size_t dst_size) {
        size_t avail = data_.size() - pos_;
        size_t take = std::min(dst_size, avail);
        std::memcpy(dst, data_.data() + pos_, take);
        pos_ += take;
        return take;
    }

    size_t pos() const { return pos_; }
    void seek(size_t p) { pos_ = p; }

    const char* data() const { return data_.data(); }
    char* data() { return data_.data(); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    void clear() { data_.clear(); pos_ = 0; }
    void reserve(size_t capacity) { data_.reserve(capacity); }

    template<typename It>
    void assign(It first, It last) { data_.assign(first, last); }

    template<typename It>
    void insert(const_iterator pos, It first, It last) { data_.insert(pos, first, last); }

    void take(CMString&& s) { data_ = std::move(s); }
    CMString release() { return std::move(data_); }

    template<typename It>
    FlyBuffer(It first, It last) : data_(first, last) {}

    FlyBuffer(FlyBuffer&&) = default;
    FlyBuffer& operator=(FlyBuffer&&) = default;
    FlyBuffer(const FlyBuffer&) = delete;
    FlyBuffer& operator=(const FlyBuffer&) = delete;

private:
    CMString data_;
    size_t pos_ = 0;
};

// Shared ownership of a FlyBuffer. Used as the carrier type for compressed
// bytes throughout the read/serve/cache data flow, enabling zero-copy transfer
// (the same buffer is shared between cache, read path, and remote serve path;
// only the wire serialization boundary copies).
using FlyBufferPtr = CMSharedPtr<FlyBuffer>;
