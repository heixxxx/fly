#include <gtest/gtest.h>
#include <agent/cpp/worker_agent.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(WorkerAgentTest, CreateWithId) {
    WorkerAgent worker(42, "127.0.0.1", 18080);
    EXPECT_EQ(worker.get_worker_id(), 42);
}

TEST(WorkerAgentTest, StartWithoutMaster) {
    WorkerAgent worker(1, "127.0.0.1", 18090);
    worker.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(worker.is_running());
    EXPECT_FALSE(worker.is_registered());
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker.is_running());
}

TEST(WorkerAgentTest, SetExecutor) {
    WorkerAgent worker(1, "127.0.0.1", 18091);
    
    TaskExecutor* executor = nullptr;
    worker.set_executor(executor);
}

TEST(WorkerAgentTest, MultipleStartStop) {
    WorkerAgent worker(1, "127.0.0.1", 18092);
    
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(worker.is_running());
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker.is_running());
    
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(worker.is_running());
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(worker.is_running());
}

}  // namespace fly