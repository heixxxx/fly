#include <gtest/gtest.h>
#include <storage/cpp/write_back_queue.h>
#include <common/cpp/common_types.h>
#include <fstream>
#include <filesystem>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace {

class WriteBackQueueTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_wbq_" + std::to_string(::getpid());
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
    req.execute = [&file_path]() {
        std::ofstream ofs(file_path);
        ofs << "hello";
        ofs.close();
    };
    req.on_complete = []() {};

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
        req.execute = [&execution_order, &mtx, i]() {
            std::lock_guard<std::mutex> lock(mtx);
            execution_order.push_back(i);
        };
        req.on_complete = []() {};
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
    blocking_req.execute = [&blocker_done, &blocker_mtx, &blocker_cv]() {
        std::unique_lock<std::mutex> lock(blocker_mtx);
        blocker_cv.wait(lock, [&blocker_done]() { return blocker_done.load(); });
    };
    blocking_req.on_complete = []() {};
    queue.enqueue(std::move(blocking_req));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    fly::WriteRequest req2;
    req2.execute = [&execute_count]() { execute_count++; };
    req2.on_complete = []() {};
    queue.enqueue(std::move(req2));

    fly::WriteRequest req3;
    req3.execute = [&execute_count]() { execute_count++; };
    req3.on_complete = []() {};
    queue.enqueue(std::move(req3));

    fly::WriteRequest req4;
    req4.execute = [&execute_count]() { execute_count++; };
    req4.on_complete = []() {};

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
        req.execute = [&completed]() { completed++; };
        req.on_complete = []() {};
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
    req.execute = [&execute_called]() { execute_called = true; };
    req.on_complete = [&complete_called]() { complete_called = true; };
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
        req.execute = [&execute_count]() { execute_count++; };
        req.on_complete = []() {};
        queue.enqueue(std::move(req));
    }

    queue.stop();
    EXPECT_EQ(execute_count.load(), 3);
}

}
