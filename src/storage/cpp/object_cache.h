#pragma once

// Two-level LRU read cache with type-erased object storage.
//
// Mirrors the Python ReadCache semantics (src/storage/py/read_cache.py):
//   - low:  compressed bytes (CMString) — hit saves disk/remote IO, still needs deserialize
//   - high: deserialized object (std::any holding CMSharedPtr<T>) — hit saves deserialize
//
// Type restoration: caller knows T at compile time (read_object<T>); get_high<T>
// does std::any_cast on the stored CMSharedPtr<T>. Type mismatch → returns nullptr.
//
// Eviction: LFU-ish score = read_count / age, 30s protection window, 1.5x hard
// limit (aligned with Python _evict).
//
// Thread-safety: all ops guarded by mutex_ (write-back thread puts, reactor/
// worker threads get/put concurrently).

#include <common/cpp/common_types.h>
#include <core/cpp/config.h>
#include <any>
#include <chrono>
#include <mutex>
#include <algorithm>

namespace fly {

class ObjectCache {
public:
    static ObjectCache& instance() {
        static ObjectCache inst;
        return inst;
    }

    // high-tier get: returns the cached CMSharedPtr<T> on hit, nullptr on miss
    // or type mismatch. Touches the entry (read_count++, last_access update).
    template<typename T>
    CMSharedPtr<T> get_high(const CMString& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = high_.find(key);
        if (it == high_.end()) return nullptr;
        // any_cast on contained shared_ptr<T>; type mismatch → nullptr.
        auto held = std::any_cast<CMSharedPtr<T>>(&it->second.value_);
        if (!held) return nullptr;
        touch_entry(it->second);
        return *held;
    }

    // low-tier get: returns {true, compressed_bytes} on hit, {false, ""} on miss.
    std::pair<bool, CMString> get_low(const CMString& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = low_.find(key);
        if (it == low_.end()) return {false, CMString{}};
        touch_entry(it->second);
        return {true, std::any_cast<CMString>(it->second.value_)};
    }

    // high-tier put: stores CMSharedPtr<T> erased as std::any. size = bytes
    // used for accounting (caller passes uncompressed size from ObjectHeader).
    template<typename T>
    void put_high(const CMString& key, CMSharedPtr<T> obj, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = high_.find(key);
        if (it != high_.end()) {
            high_bytes_ -= it->second.size_;
            it->second.value_ = std::any(std::move(obj));
            it->second.size_ = size;
            it->second.last_access_ = now_sec();
            it->second.created_at_ = it->second.last_access_;
            it->second.read_count_ = 1;
        } else {
            Entry e;
            e.value_ = std::any(std::move(obj));
            e.size_ = size;
            e.last_access_ = now_sec();
            e.created_at_ = e.last_access_;
            high_[key] = std::move(e);
        }
        high_bytes_ += size;
        evict(high_, high_bytes_);
    }

    // low-tier put: stores compressed bytes.
    void put_low(const CMString& key, CMString comp_data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = low_.find(key);
        if (it != low_.end()) {
            low_bytes_ -= it->second.size_;
            it->second.value_ = std::any(comp_data);
            it->second.size_ = size;
            it->second.last_access_ = now_sec();
            it->second.created_at_ = it->second.last_access_;
            it->second.read_count_ = 1;
        } else {
            Entry e;
            e.value_ = std::any(comp_data);
            e.size_ = size;
            e.last_access_ = now_sec();
            e.created_at_ = e.last_access_;
            low_[key] = std::move(e);
        }
        low_bytes_ += size;
        evict(low_, low_bytes_);
    }

    // remove from both tiers (used on object invalidation: remove_object/freeze).
    void remove(const CMString& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = low_.find(key);
        if (it != low_.end()) {
            low_bytes_ -= it->second.size_;
            low_.erase(it);
        }
        auto hit = high_.find(key);
        if (hit != high_.end()) {
            high_bytes_ -= hit->second.size_;
            high_.erase(hit);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        low_.clear();
        high_.clear();
        low_bytes_ = 0;
        high_bytes_ = 0;
    }

    // Test-only: reset cache state and override byte limits (production uses
    // read_cache_size config). Allows deterministic eviction tests.
    void reset_for_test(size_t max_bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        low_.clear();
        high_.clear();
        low_bytes_ = 0;
        high_bytes_ = 0;
        max_bytes_ = max_bytes;
        hard_limit_ = max_bytes + max_bytes / 2;
    }

    // test/diagnostic accessors (not for hot paths)
    size_t low_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return low_bytes_;
    }
    size_t high_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return high_bytes_;
    }
    size_t low_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return low_.size();
    }
    size_t high_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return high_.size();
    }

private:
    struct Entry {
        std::any value_;        // high: CMSharedPtr<T>; low: CMString
        size_t size_ = 0;
        double last_access_ = 0;    // seconds since epoch (monotonic-ish via steady_clock)
        double created_at_ = 0;
        int read_count_ = 1;

        double score(double now) const {
            double age = now - last_access_;
            if (age < 0.001) age = 0.001;
            return static_cast<double>(read_count_) / age;
        }
    };

    mutable std::mutex mutex_;
    CMUnorderedMap<CMString, Entry> low_;
    CMUnorderedMap<CMString, Entry> high_;
    size_t low_bytes_ = 0;
    size_t high_bytes_ = 0;

    // Limits read once from config; constants mirror Python read_cache.py.
    size_t max_bytes_ = load_max_bytes();
    size_t hard_limit_ = max_bytes_ + max_bytes_ / 2;  // 1.5x
    static constexpr double PROTECTION_SEC = 30.0;

    static size_t load_max_bytes() {
        size_t cfg = 0;
        try {
            cfg = static_cast<size_t>(Config::instance()->get_int("read_cache_size"));
        } catch (...) {}
        return cfg > 0 ? cfg : (size_t(1) << 30);  // default 1 GiB
    }

    static double now_sec() {
        return std::chrono::duration_cast<std::chrono::duration<double>>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void touch_entry(Entry& e) {
        e.last_access_ = now_sec();
        e.read_count_++;
    }

    // Eviction: collect candidates past protection window; if none and over
    // hard limit, consider all. Sort ascending by score, evict until under max.
    void evict(CMUnorderedMap<CMString, Entry>& cache, size_t& bytes) {
        if (bytes <= max_bytes_) return;
        double now = now_sec();

        CMVector<std::pair<CMString, Entry*>> candidates;
        for (auto& [k, v] : cache) {
            if (now - v.created_at_ >= PROTECTION_SEC) {
                candidates.emplace_back(k, &v);
            }
        }
        if (candidates.empty() && bytes > hard_limit_) {
            for (auto& [k, v] : cache) {
                candidates.emplace_back(k, &v);
            }
        }
        if (candidates.empty()) return;

        std::sort(candidates.begin(), candidates.end(),
                  [&](const auto& a, const auto& b) {
                      return a.second->score(now) < b.second->score(now);
                  });

        for (auto& [k, v] : candidates) {
            if (bytes <= max_bytes_) break;
            bytes -= v->size_;
            cache.erase(k);
        }
    }
};

}  // namespace fly
