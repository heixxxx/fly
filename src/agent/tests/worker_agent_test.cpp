#include <gtest/gtest.h>
#include <agent/cpp/worker_agent.h>

namespace fly {

TEST(WorkerAgentTest, CreateWithId) {
    WorkerAgent worker(42, "127.0.0.1", 18080);
    EXPECT_EQ(worker.get_worker_id(), 42);
}

TEST(WorkerAgentTest, CreateAndStart) {
    WorkerAgent worker(1, "127.0.0.1", 18080);
    worker.start();
    
    EXPECT_TRUE(worker.is_running());
    worker.stop();
    EXPECT_FALSE(worker.is_running());
}

TEST(WorkerAgentTest, MultipleWorkers) {
    WorkerAgent worker1(1, "127.0.0.1", 18080);
    WorkerAgent worker2(2, "127.0.0.1", 18080);
    WorkerAgent worker3(3, "127.0.0.1", 18080);
    
    worker1.start();
    worker2.start();
    worker3.start();
    
    EXPECT_TRUE(worker1.is_running());
    EXPECT_TRUE(worker2.is_running());
    EXPECT_TRUE(worker3.is_running());
    
    worker1.stop();
    worker2.stop();
    worker3.stop();
    
    EXPECT_FALSE(worker1.is_running());
    EXPECT_FALSE(worker2.is_running());
    EXPECT_FALSE(worker3.is_running());
}

TEST(WorkerAgentTest, MultipleStartStop) {
    WorkerAgent worker(1, "127.0.0.1", 18080);
    
    worker.start();
    EXPECT_TRUE(worker.is_running());
    
    worker.stop();
    EXPECT_FALSE(worker.is_running());
    
    worker.start();
    EXPECT_TRUE(worker.is_running());
    
    worker.stop();
    EXPECT_FALSE(worker.is_running());
}

}  // namespace fly