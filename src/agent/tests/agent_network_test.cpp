#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <agent/cpp/task_executor.h>
#include <common/cpp/test_helpers.h>
#include "test_log_isolation.h"
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

using namespace fly::test;

namespace fly {

class AgentNetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::shutdown();
        // level 0（全级别）：EndToEnd 压测卡死取证时 INFO 全滤（Drain/Executing/
        // TaskComplete 均不可见）导致误判"链路无执行"——诊断代价为零，全开。
        Logger::init("test_logs/", 0);
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

// 正常退出归类（用户裁定：正常/异常退出显式分派）：worker 本地 stop() 走
// graceful 分支——关连接前发 WORKER_EXIT 声明；master on_disconnect 依据
// 声明（exit_confirmed，非 shutdown_pending 指令场景）归类为正常退出：
// 终态 EXITED（非 DEAD）、不登记断连宽限、无判死副作用。
TEST_F(AgentNetworkTest, WorkerGracefulExitClassifiedAsExited) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    EXPECT_FALSE(master.shutdown_pending_for_testing(1))
        << "no master-initiated shutdown yet — classification must come from WORKER_EXIT";
    EXPECT_EQ(worker.exit_code(), 0) << "graceful stop before shutdown must map to exit code 0";

    worker.stop();

    // 等 on_disconnect 完成（清表 + 三分派归类）。
    wait_for([&]{ return master.get_connection_count() == 0; }, 100, 30);

    // 归类正确性的断言落在终态：WORKER_EXIT 先于 DISCONNECT（同串行 lane
    // FIFO）被消费，on_disconnect 走 handle_worker_exit → EXITED。瞬态标记
    //（exit_confirmed/shutdown_pending）在归类消费时即 erase，此处不可断言。
    EXPECT_EQ(master.worker_status_for_testing(1), WorkerStatus::EXITED)
        << "graceful exit must land in EXITED, not DEAD";
    EXPECT_EQ(master.get_idle_workers().size(), 0u)
        << "exited worker must not be schedulable";

    master.stop();
}

// worker 心跳超时失联（MASTER_LOST，abnormal）：exit_code=3、不声明正常退出；
// master 侧的死亡归类由 DisconnectReconnectsAndReports/宽限家族覆盖，此处
// 断言 worker 侧显式分支的退出码语义。
TEST_F(AgentNetworkTest, WorkerExitCodeReflectsExitReason) {
    WorkerAgent worker(1, "127.0.0.1", 1);  // 端口 1：不可连，无需 master
    EXPECT_EQ(worker.exit_code(), 0) << "default (no shutdown initiated) is graceful";

    worker.initiate_shutdown_for_testing(fly::ExitReason::MASTER_LOST, "unit test");
    EXPECT_EQ(worker.exit_code(), 3) << "MASTER_LOST must map to abnormal exit code 3";
    EXPECT_FALSE(worker.exit_reason_graceful(worker.exit_reason()));

    worker.initiate_shutdown_for_testing(fly::ExitReason::LOCAL_STOP, "unit test");
    EXPECT_EQ(worker.exit_code(), 3) << "idempotent: first-trigger reason (MASTER_LOST) wins";
    worker.stop();
}

// 注册 ack 丢失兜底（P3-23 根因修复的确定性回归）：master 吞掉首条 REGISTER
// （等效应用层丢消息——worker 无限等），注册守望以指数退避（本测 100ms 初值）
// 重发后注册成功。修复前该场景永久挂死（真实失败形态：60s 测试超时）。
// 连接级丢失不走此路径（由 on_disconnect → reconnect_loop 事件驱动恢复）。
TEST_F(AgentNetworkTest, RegisterAckLossRecoveredByResend) {
    Config::instance()->set_int("worker_register_ack_retry_initial_ms", 100);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    master.drop_next_register_for_testing_ = true;

    WorkerAgent worker(11, "127.0.0.1", master.get_port());
    worker.start();

    // 首发被吞 + ~100ms 退避重发 → 注册成功（无重发机制时此处超时失败）。
    EXPECT_TRUE(wait_until_registered(worker, 200, 50)) << "watchdog resend must recover from lost ack";

    EXPECT_EQ(master.get_connection_count(), 1);
    auto connected = master.get_connected_workers();
    ASSERT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0], 11u);

    master.stop();
    worker.stop();
    Config::instance()->set_int("worker_register_ack_retry_initial_ms", 500);
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
    // P3-23 事件序列取证：每秒记录关键状态变化（仅变化时打，失败时序列
    // 完整保留在 gtest 输出——Logger 文件在 bazel sandbox 下不可靠）。
    int last_conn = -1, last_reg = -1;
    for (int i = 0; i < 600 && !exited; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        exited = !second.is_running();
        if (i % 10 == 0) {
            int conn = static_cast<int>(master.get_connection_count());
            int reg = second.is_registered() ? 1 : 0;
            if (conn != last_conn || reg != last_reg) {
                std::cerr << "[p3-23 t=" << (i / 10) << "s] conns=" << conn
                          << " second_reg=" << reg
                          << " second_running=" << (second.is_running() ? 1 : 0) << "\n";
                last_conn = conn;
                last_reg = reg;
            }
        }
    }
    if (!exited) {
        // P3-23 确定性取证：失败现场直出 gtest 输出（bazel sandbox 会丢
        // test_logs 相对路径的 Logger 文件，经 stderr 才可靠送达 test.log）。
        std::cerr << "\n===== P3-23 SCENE (second did not exit) =====\n"
                  << "second.is_registered=" << (second.is_registered() ? 1 : 0) << "\n"
                  << "master conn count=" << master.get_connection_count() << "\n"
                  << "----- scene cwd & logs -----\n";
        // 不吞错误：pwd/ls 可见性 + /proc 双路径兜底（bazel sandbox cwd 探测）。
        std::system("pwd 1>&2; ls -la 1>&2 | head -15; "
                    "tail -n 150 test_logs/master.log 1>&2 || "
                    "tail -n 150 /proc/$PPID/cwd/test_logs/master.log 1>&2");
        std::cerr << "===== END SCENE =====\n";
    }
    EXPECT_TRUE(exited) << "duplicate worker should exit after probe-confirmed rejection";
    EXPECT_FALSE(second.is_registered());
    EXPECT_TRUE(first.is_registered());  // 先到者不受影响
    EXPECT_EQ(master.get_connected_workers().size(), 1u);

    master.stop();
    first.stop();
}

TEST_F(AgentNetworkTest, MultipleWorkers) {
    // 同 SetUp：全级别（原 2/3 把 INFO 滤掉，链路不可见）。
    
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
    // 同 SetUp：全级别（原 1/2 把 INFO 滤掉，Drain/Executing/TaskComplete 全部
    // 不可见——r233/r332 取证时被误导为"task 从未执行"）。
    
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

    // 等全部 3 个完成（原 >=2 提前 break：task 3 留在 worker 队列无人 poll，
    // master 侧 RUNNING 挂到 stop() drain——旧 30s 超时掩盖，drain 语义修正后
    // 4 实例压测实测 300s 卡死）。
    for (int i = 0; i < 200; ++i) {
        worker1.poll_task();
        worker2.poll_task();
        if (master.get_completed_tasks().size() >= 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto completed = master.get_completed_tasks();
    EXPECT_GE(completed.size(), 3);
    
    master.stop();
    worker1.stop();
    worker2.stop();
}

}  // namespace fly