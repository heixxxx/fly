// WriterPrefRwLock 本体单测（P3-17 批 2）——自研写优先 RW 锁此前零专项测试
//（唯一生产使用点 DataService::remote_mutex_）。
//
// 断言语义：
//   - 互斥：写者临界区内读者计数必为 0，反之读者计数 >0 时写者必不在临界区；
//   - 写优先：读者持续流过时写者仍能在有界时间内拿到写锁（不饿死）；
//   - try_lock/try_lock_shared 语义与锁状态一致。
#include <gtest/gtest.h>
#include <common/cpp/writer_pref_rwlock.h>
#include <common/cpp/common_types.h>
#include <atomic>
#include <chrono>
#include <latch>
#include <thread>
#include <vector>

namespace fly {
namespace {

TEST(WriterPrefRwLockTest, WriteExcludesReadsAndViceVersa) {
    WriterPrefRwLock lock;
    std::atomic<int> readers_in_cs{0};
    std::atomic<bool> writer_in_cs{false};
    std::atomic<bool> violation{false};
    std::latch go{5};
    CMVector<std::thread> threads;

    threads.emplace_back([&] {
        go.count_down(); go.wait();
        for (int i = 0; i < 500; ++i) {
            lock.lock();
            EXPECT_FALSE(writer_in_cs.load());
            writer_in_cs.store(true);
            EXPECT_EQ(readers_in_cs.load(), 0)
                << "写者临界区内出现读者（互斥破坏）";
            writer_in_cs.store(false);
            lock.unlock();
        }
    });
    for (int r = 0; r < 4; ++r) {
        threads.emplace_back([&] {
            go.count_down(); go.wait();
            for (int i = 0; i < 500; ++i) {
                lock.lock_shared();
                readers_in_cs.fetch_add(1);
                if (writer_in_cs.load()) violation.store(true);
                readers_in_cs.fetch_sub(1);
                lock.unlock_shared();
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_FALSE(violation.load()) << "读者观察到写者在临界区（互斥破坏）";
}

TEST(WriterPrefRwLockTest, WriterNotStarvedUnderContinuousReaders) {
    WriterPrefRwLock lock;
    std::atomic<bool> stop{false};
    std::latch go{4};
    CMVector<std::thread> threads;
    for (int r = 0; r < 3; ++r) {
        threads.emplace_back([&] {
            go.count_down(); go.wait();
            while (!stop.load()) {
                lock.lock_shared();
                lock.unlock_shared();
            }
        });
    }
    go.count_down(); go.wait();
    // 写优先语义：读者持续流过时写者仍应有界时间内拿到锁。
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool acquired = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (lock.try_lock()) {
            acquired = true;
            lock.unlock();
            break;
        }
    }
    stop.store(true);
    for (auto& t : threads) t.join();
    EXPECT_TRUE(acquired) << "写者 5s 内未拿到写锁——写优先失效（饿死）";
}

TEST(WriterPrefRwLockTest, TryLockReflectsHeldState) {
    WriterPrefRwLock lock;
    lock.lock();
    EXPECT_FALSE(lock.try_lock());        // 写持有时写 try 失败
    EXPECT_FALSE(lock.try_lock_shared()); // 写持有时读 try 失败
    lock.unlock();

    lock.lock_shared();
    EXPECT_TRUE(lock.try_lock_shared());  // 读持有可再共享
    EXPECT_FALSE(lock.try_lock());        // 读持有时写 try 失败
    lock.unlock_shared();
    lock.unlock_shared();

    EXPECT_TRUE(lock.try_lock());         // 空闲可写
    lock.unlock();
}

}  // namespace
}  // namespace fly
