// ConcurrentQueue 单测：背压/终态唤醒/drain/快照/多线程压测。

#include <gtest/gtest.h>
#include <common/cpp/concurrent_queue.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace fly {
namespace {

TEST(ConcurrentQueueTest, BasicPushPop) {
    ConcurrentQueue<std::string> q;
    EXPECT_TRUE(q.push("a"));
    EXPECT_TRUE(q.push("b"));
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(*q.pop(), "a");
    EXPECT_EQ(*q.try_pop(), "b");
    EXPECT_FALSE(q.try_pop().has_value());
}

TEST(ConcurrentQueueTest, BoundedCountBlocksAndRecovers) {
    ConcurrentQueue<int, CountCapacity> q(CountCapacity(2));
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    // 满：push 在后台阻塞。
    std::atomic<bool> pushed{false};
    std::thread t([&] {
        pushed = q.push(3);
    });
    // 轮询等后台线程进入阻塞（队列仍是满态且未 push 成功）。
    for (int i = 0; i < 100 && q.occupancy() != 2; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_FALSE(pushed.load()) << "push must block when full";
    EXPECT_EQ(*q.pop(), 1);   // 消费一个 → 后台 push 解除阻塞
    t.join();
    EXPECT_TRUE(pushed.load());
    EXPECT_EQ(q.size(), 2u);
}

TEST(ConcurrentQueueTest, BytesCapacityAccounting) {
    ConcurrentQueue<std::string, BytesCapacity> q(BytesCapacity(10));
    EXPECT_TRUE(q.push("12345"));
    EXPECT_TRUE(q.try_push("12345"));
    EXPECT_FALSE(q.try_push("x"));   // 10 + 1 > 10
    EXPECT_EQ(q.occupancy(), 10u);
}

TEST(ConcurrentQueueTest, CloseWakesBlockedProducer) {
    ConcurrentQueue<int, CountCapacity> q(CountCapacity(1));
    ASSERT_TRUE(q.push(1));
    std::atomic<bool> pushed{true};
    std::thread t([&] {
        pushed = q.push(2);   // 阻塞直到 close
    });
    for (int i = 0; i < 100 && q.occupancy() != 1; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    q.close();
    t.join();
    EXPECT_FALSE(pushed.load()) << "push after close must return false";
    // close 且未排空：残量仍可消费，排空后 EOF。
    EXPECT_EQ(*q.pop(), 1);
    EXPECT_FALSE(q.pop().has_value());
}

TEST(ConcurrentQueueTest, CloseWakesBlockedConsumer) {
    ConcurrentQueue<int> q;
    std::atomic<bool> got{true};
    std::thread t([&] {
        got = q.pop().has_value();   // 阻塞直到 close
    });
    // 轮询等消费者进入等待（无法直接观察 cv 状态；close 后结果可判）。
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.close();
    t.join();
    EXPECT_FALSE(got.load()) << "pop after close-and-empty must give EOF";
}

TEST(ConcurrentQueueTest, FailDropsResidual) {
    ConcurrentQueue<int> q;
    ASSERT_TRUE(q.push(1));
    q.fail();
    EXPECT_FALSE(q.pop().has_value()) << "fail must drop residual";
    EXPECT_FALSE(q.push(2));
    EXPECT_TRUE(q.failed());
}

TEST(ConcurrentQueueTest, DrainAllAndSnapshot) {
    ConcurrentQueue<int> q;
    q.push(1);
    q.push(2);
    auto snap = q.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(q.size(), 2u);   // snapshot 不清空
    auto drained = q.drain_all();
    ASSERT_EQ(drained.size(), 2u);
    EXPECT_EQ(q.size(), 0u);
}

TEST(ConcurrentQueueTest, MultiProducerConsumerStress) {
    ConcurrentQueue<int> q;
    constexpr int kPerProducer = 2000;
    constexpr int kProducers = 4;
    std::atomic<int> consumed{0};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<bool> stop{false};
    for (int p = 0; p < kProducers; p++) {
        producers.emplace_back([&q, p] {
            for (int i = 0; i < kPerProducer; i++) {
                ASSERT_TRUE(q.push(p * kPerProducer + i));
            }
        });
    }
    for (int c = 0; c < 3; c++) {
        consumers.emplace_back([&] {
            while (consumed.load() < kProducers * kPerProducer) {
                if (q.pop_for(std::chrono::milliseconds(50)).has_value()) {
                    consumed.fetch_add(1);
                }
                if (stop.load()) return;
            }
        });
    }
    for (auto& t : producers) t.join();
    while (consumed.load() < kProducers * kPerProducer) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    stop = true;
    q.close();
    for (auto& t : consumers) t.join();
    EXPECT_EQ(consumed.load(), kProducers * kPerProducer);
}

}  // namespace
}  // namespace fly
