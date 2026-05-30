#pragma once

#include <common/cpp/common_types.h>
#include <mutex>
#include <optional>
#include <map>
#include <unordered_map>
#include <utility>

namespace fly {

// Thread-safe map wrapper base. All methods lock an internal std::mutex.
// Never returns raw pointers or references to internal storage — all reads
// return by value (std::optional<Value> for lookups).
template<typename Key, typename Value, typename MapType>
class ConcurrentMapBase {
public:
    ConcurrentMapBase() = default;

    ConcurrentMapBase(const ConcurrentMapBase&) = delete;
    ConcurrentMapBase& operator=(const ConcurrentMapBase&) = delete;
    ConcurrentMapBase(ConcurrentMapBase&&) = delete;
    ConcurrentMapBase& operator=(ConcurrentMapBase&&) = delete;

    bool insert(const Key& key, const Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto [it, inserted] = map_.emplace(key, value);
        return inserted;
    }

    bool insert(const Key& key, Value&& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto [it, inserted] = map_.emplace(key, std::move(value));
        return inserted;
    }

    std::optional<Value> find(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.erase(key) > 0;
    }

    bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.find(key) != map_.end();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
    }

    std::optional<Value> at(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<Value> operator[](const Key& key) const {
        return at(key);
    }

    // Atomic find-or-create. If key exists, returns existing value.
    // Otherwise calls factory(), inserts the result, and returns it.
    // Factory is called under the lock.
    template<typename Factory>
    Value get_or_insert(const Key& key, Factory&& factory) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        Value value = factory();
        auto [inserted_it, ok] = map_.emplace(key, value);
        return inserted_it->second;
    }

    // Iterate all entries under the lock.
    // Func signature: void(const Key&, Value&)
    template<typename Func>
    void iterate(Func&& func) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [key, value] : map_) {
            func(key, value);
        }
    }

    // Const iterate all entries under the lock.
    // Func signature: void(const Key&, const Value&)
    template<typename Func>
    void iterate(Func&& func) const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [key, value] : map_) {
            func(key, value);
        }
    }

protected:
    MapType map_;
    mutable std::mutex mutex_;
};

template<typename Key, typename Value>
class ConcurrentMap
    : public ConcurrentMapBase<Key, Value, CMMap<Key, Value>> {};

template<typename Key, typename Value>
class ConcurrentUnorderedMap
    : public ConcurrentMapBase<Key, Value, CMUnorderedMap<Key, Value>> {};

} // namespace fly

using fly::ConcurrentMap;
using fly::ConcurrentUnorderedMap;
