#pragma once

// PendingRpcMap — shared infrastructure for worker_agent's synchronous
// request/ack patterns. Each instance owns the mutex + cv + map triple that
// was previously hand-written per RPC type (DbPath, WriteRegister, Freeze,
// VarOp, Remove), eliminating ~15 boilerplate member declarations and 5
// duplicated wait/notify skeletons.
//
// Design:
//   - emplace():          insert a pending entry under key (caller pre-built).
//   - wait_for():         block until notified + predicate true, or timeout.
//                         Returns the shared_ptr (or nullptr on timeout).
//                         Erases the entry on timeout.
//   - complete():         single-phase notify — lock, run filler on the entry,
//                         notify_all. Used when filler needs no pre-work
//                         outside the lock (DbPath/WriteReg/Freeze).
//   - take_for_complete(): two-phase notify — lock, pop the shared_ptr out,
//                         unlock. Caller mutates the entry outside the lock
//                         (e.g. VarOp builds a FlyBuffer), then calls
//                         notify_all/notify_one. Preserves the existing
//                         two-phase pattern needed by VarOp/Remove.
//
// Thread-safety: each instance has its own mutex, so different RPC types do
// not contend with each other (same granularity as the prior hand-written
// per-type mutexes).

#include <common/cpp/common_types.h>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace fly {

template <typename Key, typename Pending>
class PendingRpcMap {
public:
    // Insert (or overwrite) a pending entry under key. Returns the inserted ptr.
    CMSharedPtr<Pending> emplace(const Key& key, CMSharedPtr<Pending> p) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_[key] = p;
        return p;
    }

    // Look up without inserting (returns nullptr if absent).
    CMSharedPtr<Pending> find(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        return it != map_.end() ? it->second : nullptr;
    }

    // Block until is_done(pending) returns true or timeout. On success returns
    // the shared_ptr (still in the map); on timeout returns nullptr and erases.
    // is_done is invoked under the lock; it must only read the pending entry.
    template <typename Pred>
    CMSharedPtr<Pending> wait_for(const Key& key,
                                  std::chrono::milliseconds timeout,
                                  Pred is_done) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        auto& pending = it->second;
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!is_done(pending)) {
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                if (!is_done(pending)) {
                    map_.erase(it);
                    return nullptr;
                }
                break;
            }
        }
        return pending;
    }

    // Erase an entry (called by the waiter after consuming the result).
    void erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.erase(key);
    }

    // Single-phase complete: run filler on the entry under the lock, then wake
    // all waiters. filler must be cheap (field assignments only).
    template <typename Filler>
    void complete(const Key& key, Filler filler) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            filler(*it->second);
        }
        cv_.notify_all();
    }

    // Two-phase complete (step 1): pop the shared_ptr out under the lock so the
    // caller can mutate it outside the lock (e.g. constructing a FlyBuffer).
    // The entry is re-inserted by return_for_complete() after mutation.
    // Returns nullptr if the key is absent (e.g. waiter already timed out).
    CMSharedPtr<Pending> take_for_complete(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        // Leave the entry in the map but hand out a shared_ptr copy; the waiter
        // may still be blocked on it. Caller mutates via the shared_ptr.
        return it->second;
    }

    void notify_all() { cv_.notify_all(); }
    void notify_one() { cv_.notify_one(); }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    CMUnorderedMap<Key, CMSharedPtr<Pending>> map_;
};

}  // namespace fly
