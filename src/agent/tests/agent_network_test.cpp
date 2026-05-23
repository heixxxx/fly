#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <agent/cpp/task_executor.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

static bool wait_for_registered(WorkerAgent& worker, int max_attempts = 30, int delay_ms = 50) {
    for (int i = 0; i < max_attempts; ++i) {
        if (worker.is_registered()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    return worker.is_registered();
}

class AgentNetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::shutdown();
        Logger::init("test_logs/", 0);
        Logger::init("test_logs/", 1);
    }
    
    void TearDown() override {
        Logger::shutdown();
    }
};

TEST_F(AgentNetworkTest, WorkerRegister) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    EXPECT_TRUE(wait_for_registered(worker));
    EXPECT_EQ(master.get_connection_count(), 1);
    
    auto connected = master.get_connected_workers();
    EXPECT_EQ(connected.size(), 1);
    EXPECT_EQ(connected[0], 1);
    
    master.stop();
    worker.stop();
}

TEST_F(AgentNetworkTest, MultipleWorkers) {
    Logger::init("test_logs/", 2);
    Logger::init("test_logs/", 3);
    
    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint16_t port = master.get_port();
    
    WorkerAgent worker1(1, "127.0.0.1", port);
    WorkerAgent worker2(2, "127.0.0.1", port);
    WorkerAgent worker3(3, "127.0.0.1", port);
    worker1.start();
    worker2.start();
    worker3.start();
    // Retry loop: 3 workers connecting concurrently may need more time
    for (int retry = 0; retry < 10; ++retry) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (master.get_connection_count() >= 3) break;
    }
    EXPECT_EQ(master.get_connection_count(), 3);
    
    auto connected = master.get_connected_workers();
    EXPECT_EQ(connected.size(), 3);
    
    master.stop();
    worker1.stop();
    worker2.stop();
    worker3.stop();
}

TEST_F(AgentNetworkTest, WorkerDisconnect) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    
    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 1);
    
    worker.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(master.get_connection_count(), 0);
    
    master.stop();
}

TEST_F(AgentNetworkTest, MasterRestart) {
    MasterAgent master("127.0.0.1", 0);
    
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(master.is_running());
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
    
    // After restart, port may change
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
    
    WorkerAgent worker(1, "127.0.0.1", 0);
    auto exec_ptr = CMMakeShared<TaskExecutor>(std::move(executor));
    worker.set_executor(exec_ptr);
    
    auto result = exec_ptr->execute(1, "test_task", "test_module", {});
    EXPECT_EQ(result.status, TaskExecStatus::SUCCESS);
    EXPECT_EQ(result.output, "mock_result");
}

TEST_F(AgentNetworkTest, EndToEndTaskExecution) {
    Logger::init("test_logs/", 1);
    Logger::init("test_logs/", 2);
    
    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint16_t port = master.get_port();
    
    WorkerAgent worker1(1, "127.0.0.1", port);
    WorkerAgent worker2(2, "127.0.0.1", port);
    
    TaskExecutor executor1;
    executor1.set_exec_func([](uint64_t id, const CMString& name,
                                const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "executed: " + name;
        return result;
    });
    
    TaskExecutor executor2;
    executor2.set_exec_func([](uint64_t id, const CMString& name,
                                const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "executed: " + name;
        return result;
    });
    
    worker1.set_executor(CMMakeShared<TaskExecutor>(std::move(executor1)));
    worker2.set_executor(CMMakeShared<TaskExecutor>(std::move(executor2)));
    worker1.start();
    worker2.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(worker1.is_registered());
    EXPECT_TRUE(worker2.is_registered());
    
    master.submit_task(1, "test_task_1", "test_module", {"arg1"}, {}, {});
    master.submit_task(2, "test_task_2", "test_module", {"arg2"}, {}, {});
    master.submit_task(3, "test_task_3", "test_module", {"arg3"}, {}, {});
    
    for (int i = 0; i < 100; ++i) {
        worker1.poll_task();
        worker2.poll_task();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto completed = master.get_completed_tasks();
    EXPECT_GE(completed.size(), 2);
    
    master.stop();
    worker1.stop();
    worker2.stop();
}

}  // namespace fly