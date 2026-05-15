#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <agent/cpp/task_executor.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

class AgentNetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::shutdown();
        Logger::init_master("test_logs/");
        Logger::init_worker(1, "test_logs/");
    }
    
    void TearDown() override {
        Logger::shutdown();
    }
};

TEST_F(AgentNetworkTest, WorkerRegister) {
    MasterAgent master("127.0.0.1", 19080);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    WorkerAgent worker(1, "127.0.0.1", 19080);
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(worker.is_registered());
    EXPECT_EQ(master.get_connection_count(), 1);
    
    auto connected = master.get_connected_workers();
    EXPECT_EQ(connected.size(), 1);
    EXPECT_EQ(connected[0], 1);
    
    master.stop();
    worker.stop();
}

TEST_F(AgentNetworkTest, MultipleWorkers) {
    Logger::init_worker(2, "test_logs/");
    Logger::init_worker(3, "test_logs/");
    
    MasterAgent master("127.0.0.1", 19081);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    WorkerAgent worker1(1, "127.0.0.1", 19081);
    WorkerAgent worker2(2, "127.0.0.1", 19081);
    WorkerAgent worker3(3, "127.0.0.1", 19081);
    worker1.start();
    worker2.start();
    worker3.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_EQ(master.get_connection_count(), 3);
    
    auto connected = master.get_connected_workers();
    EXPECT_EQ(connected.size(), 3);
    
    master.stop();
    worker1.stop();
    worker2.stop();
    worker3.stop();
}

TEST_F(AgentNetworkTest, WorkerDisconnect) {
    MasterAgent master("127.0.0.1", 19082);
    master.start();
    
    WorkerAgent worker(1, "127.0.0.1", 19082);
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 1);
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 0);
    
    master.stop();
}

TEST_F(AgentNetworkTest, MasterRestart) {
    MasterAgent master("127.0.0.1", 19083);
    
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(master.is_running());
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
    
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(master.is_running());
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
}

TEST_F(AgentNetworkTest, ExecutorInjection) {
    TaskExecutor executor;
    executor.set_exec_func([](uint64_t id, const CMString& name,
                              const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "mock_result";
        return result;
    });
    
    WorkerAgent worker(1, "127.0.0.1", 19084);
    worker.set_executor(&executor);
    
    auto result = executor.execute(1, "test_task", "test_module", {});
    EXPECT_EQ(result.status, TaskExecStatus::SUCCESS);
    EXPECT_EQ(result.output, "mock_result");
}

}  // namespace fly