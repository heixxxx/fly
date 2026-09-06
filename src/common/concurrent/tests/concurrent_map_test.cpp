// ConcurrentMap / ConcurrentUnorderedSet 单元测试。
//
// 覆盖：基础接口（insert/find/erase/get_or_insert/iterate）、v2 新增接口
// （update 读改写、take 消费式读取、with_lock 持锁逃生口、ConcurrentSet）、
// 以及并发正确性（多线程 insert/take 总量守恒——join 后断言，无 sleep 依赖）。

#include <common/concurrent/cpp/concurrent_map.h>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace {

struct Score {
    double cumulative_ = 0.0;
    int64_t count_ = 0;
};

} // namespace

TEST(ConcurrentMapTest, InsertFindEraseBasics) {
    ConcurrentUnorderedMap<CMString, int> map;
    EXPECT_TRUE(map.insert("a", 1));
    EXPECT_FALSE(map.insert("a", 2));  // 重复 insert 不覆盖
    EXPECT_EQ(map.find("a").value(), 1);
    EXPECT_FALSE(map.contains("b"));
    EXPECT_TRUE(map.erase("a"));
    EXPECT_FALSE(map.erase("a"));
    EXPECT_EQ(map.find("a"), std::nullopt);
}

TEST(ConcurrentMapTest, UpdateRunsLambdaOnExistingEntry) {
    ConcurrentUnorderedMap<CMString, Score> map;
    map.insert("obj", Score{10.0, 1});
    map.update("obj", [](Score& s) {
        s.cumulative_ += 5.0;
        s.count_ += 1;
    });
    auto s = map.find("obj").value();
    EXPECT_DOUBLE_EQ(s.cumulative_, 15.0);
    EXPECT_EQ(s.count_, 2);
}

TEST(ConcurrentMapTest, UpdateInsertsDefaultOnMiss) {
    // 匹配原 backup_scores_ 的 operator[] 语义：miss 时默认构造插入再跑 lambda。
    ConcurrentUnorderedMap<CMString, Score> map;
    map.update("obj", [](Score& s) { s.cumulative_ = 3.0; });
    EXPECT_EQ(map.size(), 1u);
    EXPECT_DOUBLE_EQ(map.find("obj").value().cumulative_, 3.0);
}

TEST(ConcurrentMapTest, TakeReturnsValueAndErases) {
    ConcurrentUnorderedMap<CMString, Score> map;
    map.insert("obj", Score{7.0, 1});
    auto taken = map.take("obj");
    ASSERT_TRUE(taken.has_value());
    EXPECT_DOUBLE_EQ(taken->cumulative_, 7.0);
    EXPECT_FALSE(map.contains("obj"));  // take 后条目消失
    EXPECT_FALSE(map.take("obj").has_value());  // 二次 take 为空
}

TEST(ConcurrentMapTest, WithLockAccessesInternalMapAtomically) {
    // with_lock 逃生口：锁内做"遍历 + 条件 erase"复合操作（迁移 cleanup 场景）。
    ConcurrentUnorderedMap<int, int> map;
    for (int i = 0; i < 10; ++i) map.insert(i, i);
    int erased = map.with_lock([](CMUnorderedMap<int, int>& m) {
        int n = 0;
        for (auto it = m.begin(); it != m.end();) {
            if (it->second % 2 == 0) { it = m.erase(it); ++n; }
            else { ++it; }
        }
        return n;
    });
    EXPECT_EQ(erased, 5);
    EXPECT_EQ(map.size(), 5u);
    EXPECT_TRUE(map.contains(1));
    EXPECT_FALSE(map.contains(0));
}

TEST(ConcurrentMapTest, GetOrInsertCallsFactoryOnlyOnMiss) {
    ConcurrentUnorderedMap<CMString, int> map;
    std::atomic<int> factory_calls{0};
    auto factory = [&] { ++factory_calls; return 42; };
    EXPECT_EQ(map.get_or_insert("k", factory), 42);
    EXPECT_EQ(map.get_or_insert("k", factory), 42);  // 命中不调 factory
    EXPECT_EQ(factory_calls.load(), 1);
}

TEST(ConcurrentMapTest, IterateSeesAllEntries) {
    ConcurrentUnorderedMap<CMString, int> map;
    map.insert("a", 1);
    map.insert("b", 2);
    int sum = 0;
    map.iterate([&](const CMString&, const int& v) { sum += v; });
    EXPECT_EQ(sum, 3);
}

TEST(ConcurrentSetTest, InsertReportsNewAndContains) {
    ConcurrentUnorderedSet<CMString> set;
    EXPECT_TRUE(set.insert("a"));   // 新插入
    EXPECT_FALSE(set.insert("a"));  // 已存在
    EXPECT_TRUE(set.contains("a"));
    EXPECT_TRUE(set.erase("a"));
    EXPECT_FALSE(set.contains("a"));
    EXPECT_FALSE(set.erase("a"));
    set.insert("b");
    EXPECT_EQ(set.size(), 1u);
    set.clear();
    EXPECT_EQ(set.size(), 0u);
}

// 并发守恒：N 个生产者各插入 M 个唯一 key，同时 K 个消费者循环 take，
// 最终取出的条目总数 == N*M 且 map 为空。join 后断言，确定性（无 sleep）。
TEST(ConcurrentMapTest, ConcurrentInsertTakeConservation) {
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 1000;
    ConcurrentUnorderedMap<int, int> map;
    std::atomic<bool> production_done{false};
    std::atomic<int> total_taken{0};

    std::vector<std::thread> threads;
    for (int p = 0; p < kProducers; ++p) {
        threads.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                map.insert(p * kPerProducer + i, i);
            }
        });
    }
    for (int c = 0; c < kConsumers; ++c) {
        threads.emplace_back([&] {
            int local = 0;
            while (true) {
                if (map.take_any().has_value()) {
                    ++local;
                } else if (production_done.load(std::memory_order_acquire)) {
                    // 生产全部结束后再空轮询若干次仍为空才算消费完。
                    bool got = false;
                    for (int spin = 0; spin < 1000 && !got; ++spin) {
                        got = map.take_any().has_value();
                    }
                    local += got ? 1 : 0;
                    if (!got) break;
                }
            }
            total_taken += local;
        });
    }
    // 等生产者结束（join 生产者线程）
    // 注：线程数组前 kProducers 个是生产者。
    for (int p = 0; p < kProducers; ++p) threads[p].join();
    production_done.store(true, std::memory_order_release);
    for (int c = 0; c < kConsumers; ++c) threads[kProducers + c].join();

    EXPECT_EQ(total_taken.load(), kProducers * kPerProducer);
    EXPECT_EQ(map.size(), 0u);
}

// 多线程对同一 key 并发 update，计数守恒（验证 RMW 原子性）。
TEST(ConcurrentMapTest, ConcurrentUpdateOnSameKeyCountsAll) {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 1000;
    ConcurrentUnorderedMap<CMString, int> map;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                map.update("counter", [](int& v) { v += 1; });
            }
        });
    }
    for (auto& th : threads) th.join();
    EXPECT_EQ(map.find("counter").value(), kThreads * kPerThread);
}
