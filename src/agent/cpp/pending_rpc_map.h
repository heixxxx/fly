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
//                         notify_all. 字段写 + notify 全部持锁（防 lost wakeup，
//                         参见 8419526 与 on_var_ack 案例）。
//   - complete_all_if():  batch notify — filler on entries matching pred, 持锁。
//
// Thread-safety: each instance has its own mutex, so different RPC types do
// not contend with each other (same granularity as the prior hand-written
// per-type mutexes).

#include <common/cpp/common_types.h>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <utility>
#include <memory>

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

    // Insert only if absent（Problem5 防重置语义：已存在条目——尤其已
    // completed 的——不被覆盖，返回旧值）。返回 {existing_or_inserted, inserted}。
    std::pair<CMSharedPtr<Pending>, bool> insert_if_absent(const Key& key, CMSharedPtr<Pending> p) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return {it->second, false};
        }
        auto [inserted_it, ok] = map_.emplace(key, std::move(p));
        return {inserted_it->second, true};
    }

    // Look up without inserting (returns nullptr if absent).
    CMSharedPtr<Pending> find(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        return it != map_.end() ? it->second : nullptr;
    }

    // Block until is_done(pending) returns true or timeout. On success returns
    // the shared_ptr (still in the map); on timeout returns nullptr.
    // erase_on_timeout=true（默认，worker 侧 RPC 语义）：超时即 erase 防泄漏；
    // =false（merge 侧语义）：条目生命周期跨越 wait（后续 cleanup 消费），
    // 超时保留由调用方负责清理。
    // timeout<=0 = 无限等待（数据规模相关等待禁设超时：EDA 数 T 级 db 下任何
    // 正数超时都是规模假设；负 duration 的 wait_until 是"过去时间"语义会瞬间
    // 超时，此处显式分流到无期限 wait）。无限等待的安全性由调用方保证存在
    // 显式失败信号路径（ack success_=false / worker 判死联动终结期待）。
    // is_done is invoked under the lock; it must only read the pending entry.
    template <typename Pred>
    CMSharedPtr<Pending> wait_for(const Key& key,
                                  std::chrono::milliseconds timeout,
                                  Pred is_done,
                                  bool erase_on_timeout = true) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        auto& pending = it->second;
        const bool infinite = timeout.count() <= 0;
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!is_done(pending)) {
#ifdef FLY_ENABLE_TEST_HOOKS
            // 测试钩子：pred 判 false 后、进入 wait 前（仍持锁）触发。
            // 用于 latch 强制线程交错，确定性复现/防回归 cv lost wakeup。
            if (pre_sleep_hook_) pre_sleep_hook_();
#endif
            if (infinite) {
                cv_.wait(lock);
            } else if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                if (!is_done(pending)) {
                    if (erase_on_timeout) {
                        map_.erase(it);
                    }
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

    // Batch complete: run filler on every entry matching pred, wake all waiters.
    // 用于连接断开时批量 fail 该连接上的所有 pending（P2P 场景下对端断 = 全失败）。
    template <typename Pred, typename Filler>
    void complete_all_if(Pred pred, Filler filler) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [key, pending] : map_) {
            if (pred(*pending)) {
                filler(*pending);
            }
        }
        cv_.notify_all();
    }

    // 持锁逃生口：在 map 锁内对内部 map 做复合操作（持锁遍历取数、条件批量
    // erase、计数器读改写、wait 超时后读最终状态）。func 内禁止再获取本对象
    // 的锁（自死锁）或做网络/磁盘 IO。
    template <typename Func>
    decltype(auto) with_lock(Func&& func) {
        std::lock_guard<std::mutex> lock(mutex_);
        return func(map_);
    }

    // const 只读重载（诊断/测试钩子遍历）：func 收 const map 引用，禁止修改。
    template <typename Func>
    decltype(auto) with_lock(Func&& func) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return func(std::as_const(map_));
    }

    // 注意：曾有两阶段完成接口（take_for_complete + 锁外 notify_all），
    // 因锁外写字段 + 无锁 notify 构成 data race 与 cv lost wakeup 窗口
    //（on_var_ack 实际踩中，确定性复现见 pending_rpc_map_test）已删除。
    // 完成路径必须经 complete()/complete_all_if()（字段写 + notify 全持锁）；
    // 需要锁外预处理时，先算好结果再在 complete 的 filler 里做字段赋值。

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    CMUnorderedMap<Key, CMSharedPtr<Pending>> map_;

#ifdef FLY_ENABLE_TEST_HOOKS
public:
    // 仅测试用（release 编译零开销）：wait_for 每次循环 pred==false 后、
    // 进入 cv wait 前（仍持锁）调用。见 wait_for 内注释。
    std::function<void()> pre_sleep_hook_;
#endif
};

}  // namespace fly
