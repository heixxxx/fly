#pragma once

// WriterPrefRwLock —— 写者优先读写锁（基于 pthread_rwlock）。
//
// 为什么不用 std::shared_mutex：g++ 实现是读者优先，CPU 饥饿下读者持锁被
// OS 抢占会饿死写者（update_remote_idx/add_remote_location 长期拿不到锁，
// remote_idx 活锁）。
//
// 为什么不用手写 mutex+cv：旧实现所有读者共享一把 std::mutex，读者之间
// 完全串行，丧失读写锁的意义。本实现 reader fast path 是单次 atomic add，
// 无锁、无内核进入（glibc __pthread_rwlock_rdlock_full64 的 fast path）。
//
// 写者优先语义：写者申请后，后续到达的读者阻塞直到写者完成。glibc 通过
// PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP 实现（注意：PREFER_WRITER_NP
// 被 glibc 忽略，等同于 PREFER_READER_NP；必须用 NONRECURSIVE 变体）。
//
// 约束：本锁禁止同一线程递归持读锁（NONRECURSIVE 语义）。当前所有调用点
// （DataService::lookup_remote_idx/has_remote_location/get_remote_size/
//  maybe_suggest_backup 等）都是各自独立获取，无嵌套，已确认安全。
//
// 兼容性：提供 Lockable + SharedLockable，可直接用 std::unique_lock /
// std::shared_lock。

#include <pthread.h>
#include <system_error>

namespace fly {

class WriterPrefRwLock {
public:
    WriterPrefRwLock() {
        pthread_rwlockattr_t attr;
        pthread_rwlockattr_init(&attr);
        // PREFER_WRITER_NONRECURSIVE_NP 是 glibc 唯一真正防写者饥饿的 kind。
        pthread_rwlockattr_setkind_np(
            &attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
        const int rc = pthread_rwlock_init(&rwlock_, &attr);
        pthread_rwlockattr_destroy(&attr);
        if (rc != 0) {
            throw std::system_error(rc, std::system_category(),
                                    "pthread_rwlock_init");
        }
    }
    ~WriterPrefRwLock() { pthread_rwlock_destroy(&rwlock_); }
    WriterPrefRwLock(const WriterPrefRwLock&) = delete;
    WriterPrefRwLock& operator=(const WriterPrefRwLock&) = delete;

    // ── 写者（独占）──
    void lock()     { check(pthread_rwlock_wrlock(&rwlock_), "wrlock"); }
    bool try_lock() { return try_(pthread_rwlock_trywrlock(&rwlock_)); }
    void unlock()   { pthread_rwlock_unlock(&rwlock_); }

    // ── 读者（共享）── 禁止同线程递归持读锁。
    void lock_shared()     { check(pthread_rwlock_rdlock(&rwlock_), "rdlock"); }
    bool try_lock_shared() { return try_(pthread_rwlock_tryrdlock(&rwlock_)); }
    void unlock_shared()   { pthread_rwlock_unlock(&rwlock_); }

private:
    static void check(int rc, const char* what) {
        if (rc != 0) {
            throw std::system_error(rc, std::system_category(), what);
        }
    }
    static bool try_(int rc) {
        if (rc == 0) return true;
        if (rc == EBUSY || rc == EAGAIN) return false;
        throw std::system_error(rc, std::system_category(), "try_*");
    }
    pthread_rwlock_t rwlock_;
};

}  // namespace fly
