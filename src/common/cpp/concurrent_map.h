#pragma once

#include <common/cpp/common_types.h>
#include <mutex>
#include <optional>
#include <utility>

namespace fly {

// Thread-safe map wrapper base. All methods lock an internal std::mutex.
// Never returns raw pointers or references to internal storage — all reads
// return by value (std::optional<Value> for lookups).
//
// 复合操作接口（update/take/take_any/with_lock）覆盖"读改写 / 消费式读取 /
// 遍历+条件 erase"等多次加锁拼不出来的原子语义；with_lock 是最后逃生口，
// 优先用专用接口。Value 需可拷贝（move-only 请用 with_lock 自行处理）。
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

    // 消费式读取：原子地 erase 并返回条目（"遍历/处理 + 删除"两步拼合的原子版）。
    std::optional<Value> take(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }
        Value value = std::move(it->second);
        map_.erase(it);
        return value;
    }

    // 取出任意一个条目并 erase（空返回 nullopt）。消费端批处理场景用。
    std::optional<std::pair<Key, Value>> take_any() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (map_.empty()) {
            return std::nullopt;
        }
        auto it = map_.begin();
        auto entry = std::make_pair(it->first, std::move(it->second));
        map_.erase(it);
        return entry;
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
        auto [inserted_it, ok] = map_.emplace(key, std::move(value));
        return inserted_it->second;
    }

    // Atomic read-modify-write. If key is absent, a default-constructed Value
    // is inserted first (matches原 operator[]-under-lock 语义). updater runs
    // under the lock with a reference to the entry.
    template<typename Updater>
    void update(const Key& key, Updater&& updater) {
        std::lock_guard<std::mutex> lock(mutex_);
        updater(map_[key]);
    }

    // Last-resort escape hatch: run func on the internal map under the lock.
    // 优先用上面的专用接口；只有复合操作（遍历中 erase、跨条目原子变换等）
    // 才用 with_lock。func 内禁止再获取本对象（自死锁）或做网络/磁盘 IO。
    template<typename Func>
    decltype(auto) with_lock(Func&& func) {
        std::lock_guard<std::mutex> lock(mutex_);
        return func(map_);
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

// Thread-safe unordered set. insert 返回是否新插入（去重判定一次完成，
// 副作用可据此在锁外执行恰好一次）。
template<typename Key>
class ConcurrentUnorderedSet {
public:
    ConcurrentUnorderedSet() = default;

    ConcurrentUnorderedSet(const ConcurrentUnorderedSet&) = delete;
    ConcurrentUnorderedSet& operator=(const ConcurrentUnorderedSet&) = delete;
    ConcurrentUnorderedSet(ConcurrentUnorderedSet&&) = delete;
    ConcurrentUnorderedSet& operator=(ConcurrentUnorderedSet&&) = delete;

    bool insert(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.insert(key).second;
    }

    bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.find(key) != set_.end();
    }

    bool erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.erase(key) > 0;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        set_.clear();
    }

private:
    CMUnorderedSet<Key> set_;
    mutable std::mutex mutex_;
};

} // namespace fly

using fly::ConcurrentMap;
using fly::ConcurrentUnorderedMap;
using fly::ConcurrentUnorderedSet;
