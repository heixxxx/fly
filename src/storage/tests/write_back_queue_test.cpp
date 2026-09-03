#include <gtest/gtest.h>
#include <storage/cpp/write_back_queue.h>
#include <common/cpp/common_types.h>
#include <common/cpp/test_helpers.h>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <latch>
#include <stdexcept>

namespace {

class WriteBackQueueTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = fly::test::qa_tmp_dir("fly_test_wbq");
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(WriteBackQueueTest, StartStop) {
    fly::WriteBackQueue queue;
    EXPECT_FALSE(queue.is_running());

    queue.start();
    EXPECT_TRUE(queue.is_running());

    queue.stop();
    EXPECT_FALSE(queue.is_running());
}

TEST_F(WriteBackQueueTest, BasicEnqueueAndDrain) {
    fly::WriteBackQueue queue;
    queue.start();

    CMString file_path = test_dir_ + "/basic.txt";
    fly::WriteRequest req;
    req.execute_ = [&file_path]() -> bool {
        std::ofstream ofs(file_path);
        ofs << "hello";
        ofs.close();
        return true;
    };
    req.on_complete_ = []() {};

    queue.enqueue(std::move(req));
    queue.drain();

    std::ifstream ifs(file_path);
    CMString content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "hello");

    queue.stop();
}

TEST_F(WriteBackQueueTest, MultipleTasksInOrder) {
    fly::WriteBackQueue queue;
    queue.start();

    CMVector<int> execution_order;
    std::mutex mtx;

    for (int i = 0; i < 5; i++) {
        fly::WriteRequest req;
        req.execute_ = [&execution_order, &mtx, i]() -> bool {
            std::lock_guard<std::mutex> lock(mtx);
            execution_order.push_back(i);
        return true;
        };
        req.on_complete_ = []() {};
        queue.enqueue(std::move(req));
    }

    queue.drain();

    ASSERT_EQ(execution_order.size(), 5u);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(execution_order[i], i);
    }

    queue.stop();
}

TEST_F(WriteBackQueueTest, BackpressureAtThreshold) {
    fly::WriteBackQueue queue(1);
    queue.start();

    std::atomic<int> execute_count{0};
    std::atomic<bool> blocker_done{false};
    std::mutex blocker_mtx;
    std::condition_variable blocker_cv;

    fly::WriteRequest blocking_req;
    blocking_req.execute_ = [&blocker_done, &blocker_mtx, &blocker_cv]() -> bool {
        std::unique_lock<std::mutex> lock(blocker_mtx);
        blocker_cv.wait(lock, [&blocker_done]() { return blocker_done.load(); });
        return true;
    };
    blocking_req.on_complete_ = []() {};
    queue.enqueue(std::move(blocking_req));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    fly::WriteRequest req2;
    req2.execute_ = [&execute_count]() -> bool { execute_count++; return true; };
    req2.on_complete_ = []() {};
    queue.enqueue(std::move(req2));

    fly::WriteRequest req3;
    req3.execute_ = [&execute_count]() -> bool { execute_count++; return true; };
    req3.on_complete_ = []() {};
    queue.enqueue(std::move(req3));

    fly::WriteRequest req4;
    req4.execute_ = [&execute_count]() -> bool { execute_count++; return true; };
    req4.on_complete_ = []() {};

    std::atomic<bool> enqueue_done{false};
    std::thread enqueue_thread([&]() {
        queue.enqueue(std::move(req4));
        enqueue_done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(enqueue_done.load());

    {
        std::lock_guard<std::mutex> lock(blocker_mtx);
        blocker_done = true;
    }
    blocker_cv.notify_all();

    enqueue_thread.join();
    EXPECT_TRUE(enqueue_done.load());

    queue.drain();
    EXPECT_EQ(execute_count.load(), 3);
    queue.stop();
}

TEST_F(WriteBackQueueTest, DrainWaitsForAllTasks) {
    fly::WriteBackQueue queue;
    queue.start();

    std::atomic<int> completed{0};

    for (int i = 0; i < 5; i++) {
        fly::WriteRequest req;
        req.execute_ = [&completed]() -> bool { completed++; return true; };
        req.on_complete_ = []() {};
        queue.enqueue(std::move(req));
    }

    queue.drain();
    EXPECT_EQ(completed.load(), 5);
    EXPECT_EQ(queue.pending_count(), 0u);

    queue.stop();
}

TEST_F(WriteBackQueueTest, CompletionCallbackCalled) {
    fly::WriteBackQueue queue;
    queue.start();

    std::atomic<bool> execute_called{false};
    std::atomic<bool> complete_called{false};

    fly::WriteRequest req;
    req.execute_ = [&execute_called]() -> bool { execute_called = true; return true; };
    req.on_complete_ = [&complete_called]() { complete_called = true; };
    queue.enqueue(std::move(req));

    queue.drain();

    EXPECT_TRUE(execute_called.load());
    EXPECT_TRUE(complete_called.load());

    queue.stop();
}

TEST_F(WriteBackQueueTest, StopDrainsRemaining) {
    fly::WriteBackQueue queue;
    queue.start();

    std::atomic<int> execute_count{0};

    for (int i = 0; i < 3; i++) {
        fly::WriteRequest req;
        req.execute_ = [&execute_count]() -> bool { execute_count++; return true; };
        req.on_complete_ = []() {};
        queue.enqueue(std::move(req));
    }

    queue.stop();
    EXPECT_EQ(execute_count.load(), 3);
}

// =============================================================================
// clear_pending 测试 —— 清空未处理请求（task 异常撤销用）
// =============================================================================

TEST_F(WriteBackQueueTest, ClearPendingDropsQueuedTasks) {
    // 用一个 blocking 请求卡住 worker_loop，再 enqueue 几个，clear_pending 后
    // 这些未开始的应被丢弃（execute/on_complete 都不执行）。
    fly::WriteBackQueue queue(1000);
    queue.start();

    std::atomic<bool> blocker_done{false};
    std::mutex blocker_mtx;
    std::condition_variable blocker_cv;

    // 第一个请求：阻塞 worker_loop
    fly::WriteRequest blocking_req;
    blocking_req.execute_ = [&blocker_done, &blocker_mtx, &blocker_cv]() -> bool {
        std::unique_lock<std::mutex> lock(blocker_mtx);
        blocker_cv.wait(lock, [&blocker_done]() { return blocker_done.load(); });
        return true;
    };
    blocking_req.on_complete_ = []() {};
    queue.enqueue(std::move(blocking_req));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // 确保阻塞请求已开始执行

    // enqueue 3 个后续请求（此时 worker_loop 被卡住，它们都在 queue 里未处理）
    std::atomic<int> dropped_execute_count{0};
    std::atomic<int> dropped_complete_count{0};
    for (int i = 0; i < 3; i++) {
        fly::WriteRequest req;
        req.execute_ = [&dropped_execute_count]() -> bool { dropped_execute_count++; return true; };
        req.on_complete_ = [&dropped_complete_count]() { dropped_complete_count++; };
        queue.enqueue(std::move(req));
    }

    EXPECT_EQ(dropped_execute_count.load(), 0);

    // clear_pending：丢弃 queue 中未处理的 3 个请求
    queue.clear_pending();

    // 解除阻塞，让 worker_loop 完成 blocking_req
    {
        std::lock_guard<std::mutex> lock(blocker_mtx);
        blocker_done = true;
    }
    blocker_cv.notify_all();

    queue.drain();

    // 被丢弃的请求：execute 和 on_complete 都不应执行
    EXPECT_EQ(dropped_execute_count.load(), 0);
    EXPECT_EQ(dropped_complete_count.load(), 0);

    queue.stop();
}

TEST_F(WriteBackQueueTest, ClearPendingThenEnqueueAgain) {
    // clear_pending 后队列应能正常继续 enqueue 和处理
    fly::WriteBackQueue queue;
    queue.start();

    // enqueue 几个然后立刻 clear（worker_loop 可能还没开始处理）
    std::atomic<int> count{0};
    for (int i = 0; i < 3; i++) {
        fly::WriteRequest req;
        req.execute_ = [&count]() -> bool { count++; return true; };
        req.on_complete_ = []() {};
        queue.enqueue(std::move(req));
    }
    queue.clear_pending();

    // 再 enqueue 一个，应该正常处理
    std::atomic<bool> done{false};
    fly::WriteRequest req;
    req.execute_ = [&done]() -> bool { done = true; return true; };
    req.on_complete_ = []() {};
    queue.enqueue(std::move(req));
    queue.drain();

    EXPECT_TRUE(done.load());

    queue.stop();
}


// ════════════════════════════════════════════════════════════════════
// write-back 错误处理（P1-8）：execute_ 失败重试 / 异常隔离 / on_complete_
// 抛异常不拖垮 worker 线程。
// ════════════════════════════════════════════════════════════════════

// execute_ 失败两次第三次成功 → 重试机制收尾完成，on_complete_ 恰一次。
TEST_F(WriteBackQueueTest, ExecuteRetriesThenSucceeds) {
    fly::WriteBackQueue queue;
    queue.start();

    std::atomic<int> attempts{0};
    std::atomic<int> completions{0};

    fly::WriteRequest req;
    req.execute_ = [&attempts]() -> bool {
        return attempts.fetch_add(1) + 1 >= 3;  // 前两次失败
    };
    req.on_complete_ = [&completions]() { completions.fetch_add(1); };
    queue.enqueue(std::move(req));

    queue.drain();  // 若重试机制缺失 → graceful_exit / 永挂
    EXPECT_EQ(attempts.load(), 3);
    EXPECT_EQ(completions.load(), 1);

    queue.stop();
}

// 注：execute_ 抛异常的生产语义 = catch 后走 graceful_exit（零容忍退出进程，
// 非进程内隔离）——该路径无法在进程内测试锚定，不做收录。

// on_complete_ 抛异常 → 捕获隔离（worker 线程存活），drain 完成。
TEST_F(WriteBackQueueTest, CompleteCallbackThrowIsContained) {
    fly::WriteBackQueue queue;
    queue.start();

    std::atomic<bool> executed{false};
    fly::WriteRequest req;
    req.execute_ = [&executed]() -> bool { executed = true; return true; };
    req.on_complete_ = []() { throw std::runtime_error("cb boom"); };
    queue.enqueue(std::move(req));

    queue.drain();
    EXPECT_TRUE(executed.load());
    EXPECT_NO_THROW(queue.stop());
}

// ── 并发正确性（P3-17 批 2）：多生产者入队 + drain/stop 竞态——
//    execute 与 on_complete 一一配对守恒，join 后 pending 归零。 ──

TEST_F(WriteBackQueueTest, MultiProducerEnqueueDrainConserve) {
    fly::WriteBackQueue queue;   // high_watermark=10：4 生产者必触发背压等待
    queue.start();
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 100;
    std::atomic<int> executed{0};
    std::atomic<int> completed{0};
    std::latch go{kProducers};
    CMVector<std::thread> threads;
    for (int t = 0; t < kProducers; ++t) {
        threads.emplace_back([&, t] {
            go.count_down(); go.wait();
            for (int i = 0; i < kPerProducer; ++i) {
                fly::WriteRequest req;
                req.execute_ = [&executed]() -> bool {
                    executed.fetch_add(1);
                    return true;
                };
                req.on_complete_ = [&completed]() { completed.fetch_add(1); };
                queue.enqueue(std::move(req));
            }
        });
    }
    for (auto& th : threads) th.join();
    queue.drain();
    EXPECT_EQ(executed.load(), kProducers * kPerProducer);
    EXPECT_EQ(completed.load(), kProducers * kPerProducer);
    EXPECT_EQ(queue.pending_count(), 0u);
    queue.stop();
}

TEST_F(WriteBackQueueTest, StopAfterConcurrentProducersExecutesAllAccepted) {
    fly::WriteBackQueue queue;
    queue.start();
    constexpr int kProducers = 3;
    constexpr int kPerProducer = 60;
    std::atomic<int> executed{0};
    std::latch go{kProducers};
    CMVector<std::thread> threads;
    for (int t = 0; t < kProducers; ++t) {
        threads.emplace_back([&, t] {
            go.count_down(); go.wait();
            for (int i = 0; i < kPerProducer; ++i) {
                fly::WriteRequest req;
                req.execute_ = [&executed]() -> bool {
                    executed.fetch_add(1);
                    return true;
                };
                queue.enqueue(std::move(req));
            }
        });
    }
    for (auto& th : threads) th.join();
    queue.stop();   // stop 语义：已入队单元全部执行完再退（StopDrainsRemaining 同款）
    EXPECT_EQ(executed.load(), kProducers * kPerProducer);
}

}
