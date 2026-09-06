#pragma once

// Single-tier LRU read cache with type-erased object storage.
//
// T4（2026-08-31）对齐 Python ReadCache 双池裁定：压缩字节 low_ 池删除
//（读恒走数据源 §4.7，low 池零生产消费——仅测试调用的过期 API）。保留
// high 层：反序列化后的 typed C++ 对象（read_object<T> 的命中快路径）。
//
// Type restoration: caller knows T at compile time (read_object<T>); get_high<T>
// does std::any_cast on the stored CMSharedPtr<T>. Type mismatch → returns nullptr.
//
// Eviction: LFU-ish score = read_count / age, 30s protection window, 1.5x hard
// limit.
//
// Thread-safety: all ops guarded by mutex_ (write-back thread puts, reactor/
// worker threads get/put concurrently).

#include <container/cpp/container_aliases.h>
#include <core/cpp/config.h>
#include <common/buffer/cpp/fly_buffer.h>
#include <any>
#include <atomic>
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

    // get: returns the cached CMSharedPtr<T> on hit, nullptr on miss or type
    // mismatch. Touches the entry (read_count++, last_access update).
    template<typename T>
    CMSharedPtr<T> get_high(const CMString& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = high_.find(key);
        if (it == high_.end()) { stats_.high_misses.fetch_add(1, std::memory_order_relaxed); return nullptr; }
        // any_cast on contained shared_ptr<T>; type mismatch → nullptr.
        auto held = std::any_cast<CMSharedPtr<T>>(&it->second.value_);
        if (!held) { stats_.high_misses.fetch_add(1, std::memory_order_relaxed); return nullptr; }
        touch_entry(it->second);
        stats_.high_hits.fetch_add(1, std::memory_order_relaxed);
        return *held;
    }

    // put: stores CMSharedPtr<T> erased as std::any. size = bytes used for
    // accounting (caller passes uncompressed size from ObjectHeader).
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
        stats_.high_puts.fetch_add(1, std::memory_order_relaxed);
        stats_.high_evictions.fetch_add(evict(high_, high_bytes_), std::memory_order_relaxed);
    }

    // remove (used on object invalidation: remove_object/freeze).
    void remove(const CMString& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto hit = high_.find(key);
        if (hit != high_.end()) {
            high_bytes_ -= hit->second.size_;
            high_.erase(hit);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        high_.clear();
        high_bytes_ = 0;
        reset_stats();
    }

    // Test-only: reset cache state and override byte limits (production uses
    // read_cache_size config). Allows deterministic eviction tests.
    void reset_for_test(size_t max_bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        high_.clear();
        high_bytes_ = 0;
        max_bytes_ = max_bytes;
        hard_limit_ = max_bytes + max_bytes / 2;
        reset_stats();
    }

    // Reset all hit/miss/put/eviction counters to zero.
    void reset_stats() {
        stats_.high_hits.store(0, std::memory_order_relaxed);
        stats_.high_misses.store(0, std::memory_order_relaxed);
        stats_.high_puts.store(0, std::memory_order_relaxed);
        stats_.high_evictions.store(0, std::memory_order_relaxed);
    }

    // test/diagnostic accessors (not for hot paths)
    size_t high_bytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return high_bytes_;
    }
    size_t high_size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return high_.size();
    }

    // ── Hit statistics (hits / misses / puts / evictions) ──
    // Counters are std::atomic for lock-free reads; writes happen under mutex_
    // (or via atomic fetch_add in the hot path).
    struct Stats {
        std::atomic<uint64_t> high_hits{0};
        std::atomic<uint64_t> high_misses{0};
        std::atomic<uint64_t> high_puts{0};
        std::atomic<uint64_t> high_evictions{0};
    };

    const Stats& stats() const { return stats_; }

    // Convenience: hit rate (0.0 if no accesses yet).
    double high_hit_rate() const {
        uint64_t h = stats_.high_hits.load(), m = stats_.high_misses.load();
        uint64_t total = h + m;
        return total == 0 ? 0.0 : static_cast<double>(h) / total;
    }

private:
    struct Entry {
        std::any value_;        // CMSharedPtr<T>
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
    CMUnorderedMap<CMString, Entry> high_;
    size_t high_bytes_ = 0;
    Stats stats_;

    // Limits read once from config.
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
    // Returns the number of entries evicted (for stats accounting).
    size_t evict(CMUnorderedMap<CMString, Entry>& cache, size_t& bytes) {
        if (bytes <= max_bytes_) return 0;
        double now = now_sec();
        size_t evicted = 0;
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
        if (candidates.empty()) return 0;

        std::sort(candidates.begin(), candidates.end(),
                  [&](const auto& a, const auto& b) {
                      return a.second->score(now) < b.second->score(now);
                  });

        for (auto& [k, v] : candidates) {
            if (bytes <= max_bytes_) break;
            bytes -= v->size_;
            cache.erase(k);
            ++evicted;
        }
        return evicted;
    }
};

}  // namespace fly
