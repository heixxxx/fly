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

    const char* data() const { return data_.data(); }
    char* data() { return data_.data(); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    void clear() { data_.clear(); }
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
};
