#include <gtest/gtest.h>
#include <network/cpp/io_thread_pool.h>
#include <chrono>
#include <atomic>

namespace fly {

TEST(IOThreadPoolTest, SubmitAndProcessCompletions) {
    IOThreadPool pool(2);
    pool.start();
    
    std::atomic<int> completed{0};
    
    pool.submit([] { }, [&] { completed++; });
    pool.submit([] { }, [&] { completed++; });

    ASSERT_TRUE(pool.wait_for_completion([&]{ return completed.load() >= 2; }));

    EXPECT_GE(completed.load(), 2);
    EXPECT_EQ(pool.queue_size(), 0);
    
    pool.stop();
}

TEST(IOThreadPoolTest, GracefulShutdown) {
    IOThreadPool pool(1);
    pool.start();
    
    std::atomic<bool> task_done{false};
    std::atomic<bool> completion_done{false};
    
    pool.submit(
        [&] { 
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            task_done = true;
        },
        [&] { completion_done = true; }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(pool.is_idle());

    pool.stop();
    
    EXPECT_TRUE(task_done.load());
    pool.process_completions();
    EXPECT_TRUE(completion_done.load());
}

TEST(IOThreadPoolTest, TaskQueuing) {
    IOThreadPool pool(1);
    
    for (int i = 0; i < 10; i++) {
        pool.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }, nullptr);
    }

    EXPECT_GT(pool.queue_size(), 0);

    pool.start();
    ASSERT_TRUE(pool.wait_for_completion([&]{ return pool.queue_size() == 0 && pool.is_idle(); }, 2000));
    pool.process_completions();
    
    pool.stop();
    EXPECT_EQ(pool.queue_size(), 0);
}

TEST(IOThreadPoolTest, IsIdleAfterStop) {
    IOThreadPool pool(2);
    pool.start();
    
    pool.submit([] { }, [] { });

    ASSERT_TRUE(pool.wait_for_completion([&]{ return pool.is_idle(); }, 500));

    pool.stop();
    EXPECT_TRUE(pool.is_idle());
}

TEST(IOThreadPoolTest, MultipleThreadCount) {
    IOThreadPool pool(4);
    pool.start();
    
    std::atomic<int> counter{0};
    
    for (int i = 0; i < 20; i++) {
        pool.submit([&] { counter++; }, nullptr);
    }

    ASSERT_TRUE(pool.wait_for_completion([&]{ return counter.load() >= 20; }, 1000));

    EXPECT_EQ(counter.load(), 20);

    pool.stop();
}

}  // namespace fly