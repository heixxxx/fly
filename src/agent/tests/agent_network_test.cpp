#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <agent/cpp/task_executor.h>
#include <common/cpp/test_helpers.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

using namespace fly::test;

namespace fly {

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
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    EXPECT_TRUE(wait_until_registered(worker));
    EXPECT_EQ(master.get_connection_count(), 1);

    auto connected = master.get_connected_workers();
    EXPECT_EQ(connected.size(), 1);
    EXPECT_EQ(connected[0], 1);

    master.stop();
    worker.stop();
}

// 重复注册防护（先到先得 + 活性探测）：同 worker_id 的第二个实例注册时，
// master 先向既有连接发探测——旧实例活着应答 → 后到者的注册超时重发后被
// duplicate ack 拒绝、自行退出；先到者不受影响。（重连竞态场景——旧连接
// 是 EOF 未处理的残留——由 DisconnectReconnectsAndReports 覆盖：探测无应答
// + EOF 清表 → 重发注册被正常接受。）
TEST_F(AgentNetworkTest, DuplicateWorkerRegisterRejectedAfterProbe) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    uint16_t port = master.get_port();

    WorkerAgent first(7, "127.0.0.1", port);
    first.start();
    EXPECT_TRUE(wait_until_registered(first));

    // 同 id 的后到实例：首次注册被挂起（探测），注册超时重发后经 ProbeAck
    // 确认先到者存活 → duplicate ack → 退出。等待上限 60s：高负载（bazel
    // 并行跑全部单测）下 probe 往返 + second 的注册 ack 超时（10s）重试
    // 可能叠加 2-3 轮，30s 上限曾实测偶发不够（50 轮稳定性第 25 轮）。
    WorkerAgent second(7, "127.0.0.1", port);
    second.start();
    bool exited = false;
    for (int i = 0; i < 600 && !exited; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        exited = !second.is_running();
    }
    EXPECT_TRUE(exited) << "duplicate worker should exit after probe-confirmed rejection";
    EXPECT_FALSE(second.is_registered());
    EXPECT_TRUE(first.is_registered());  // 先到者不受影响
    EXPECT_EQ(master.get_connected_workers().size(), 1u);

    master.stop();
    first.stop();
}

TEST_F(AgentNetworkTest, MultipleWorkers) {
    Logger::init("test_logs/", 2);
    Logger::init("test_logs/", 3);
    
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    uint16_t port = master.get_port();
    
    WorkerAgent worker1(1, "127.0.0.1", port);
    WorkerAgent worker2(2, "127.0.0.1", port);
    WorkerAgent worker3(3, "127.0.0.1", port);
    worker1.start();
    worker2.start();
    worker3.start();
    wait_for([&]{ return master.get_connection_count() >= 3; });
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
    wait_for([&]{ return master.get_connection_count() >= 1; });
    
    EXPECT_EQ(master.get_connection_count(), 1);
    
    worker.stop();
    wait_for([&]{ return master.get_connection_count() == 0; }, 100, 10);
    
    EXPECT_EQ(master.get_connection_count(), 0);
    
    master.stop();
}

TEST_F(AgentNetworkTest, MasterRestart) {
    MasterAgent master("127.0.0.1", 0);
    
    master.start();
    wait_for_running(master, true);
    EXPECT_TRUE(master.is_running());
    master.stop();
    wait_for_running(master, false);
    EXPECT_FALSE(master.is_running());
    
    // After restart, port may change
    master.start();
    wait_for_running(master, true);
    EXPECT_TRUE(master.is_running());
    master.stop();
    wait_for_running(master, false);
    EXPECT_FALSE(master.is_running());
}

TEST_F(AgentNetworkTest, ExecutorInjection) {
    TaskExecutor executor;
    executor.set_exec_func([](uint64_t id, const CMString& name,
                              const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id_ = id;
        result.status_ = TaskExecStatus::SUCCESS;
        result.output_ = "mock_result";
        return result;
    });
    
    WorkerAgent worker(1, "127.0.0.1", 0);
    auto exec_ptr = CMMakeShared<TaskExecutor>(std::move(executor));
    worker.set_executor(exec_ptr);
    
    auto result = exec_ptr->execute(1, "test_task", "test_module", {});
    EXPECT_EQ(result.status_, TaskExecStatus::SUCCESS);
    EXPECT_EQ(result.output_, "mock_result");
}

TEST_F(AgentNetworkTest, EndToEndTaskExecution) {
    Logger::init("test_logs/", 1);
    Logger::init("test_logs/", 2);
    
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    uint16_t port = master.get_port();
    
    WorkerAgent worker1(1, "127.0.0.1", port);
    WorkerAgent worker2(2, "127.0.0.1", port);
    
    TaskExecutor executor1;
    executor1.set_exec_func([](uint64_t id, const CMString& name,
                                const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id_ = id;
        result.status_ = TaskExecStatus::SUCCESS;
        result.output_ = "executed: " + name;
        return result;
    });
    
    TaskExecutor executor2;
    executor2.set_exec_func([](uint64_t id, const CMString& name,
                                const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id_ = id;
        result.status_ = TaskExecStatus::SUCCESS;
        result.output_ = "executed: " + name;
        return result;
    });
    
    worker1.set_executor(CMMakeShared<TaskExecutor>(std::move(executor1)));
    worker2.set_executor(CMMakeShared<TaskExecutor>(std::move(executor2)));
    worker1.start();
    worker2.start();
    
    wait_until_registered(worker1);
    wait_until_registered(worker2);
    EXPECT_TRUE(worker1.is_registered());
    EXPECT_TRUE(worker2.is_registered());
    
    master.submit_task(1, "test_task_1", "test_module", {"arg1"}, {}, {});
    master.submit_task(2, "test_task_2", "test_module", {"arg2"}, {}, {});
    master.submit_task(3, "test_task_3", "test_module", {"arg3"}, {}, {});
    
    for (int i = 0; i < 200; ++i) {
        worker1.poll_task();
        worker2.poll_task();
        if (master.get_completed_tasks().size() >= 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto completed = master.get_completed_tasks();
    EXPECT_GE(completed.size(), 2);
    
    master.stop();
    worker1.stop();
    worker2.stop();
}

}  // namespace fly