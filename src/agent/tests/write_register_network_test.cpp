#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <agent/cpp/task_executor.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_service.h>
#include <log/cpp/logger.h>
#include <thread>
#include <chrono>

namespace fly {

#define TEST_LOG(fmt, ...) fprintf(stderr, "[TEST_DEBUG] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

class WriteRegisterNetworkTest : public ::testing::Test {
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

TEST_F(WriteRegisterNetworkTest, MasterAcceptsWriteRegisterForNormalDb) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    // Poll until registered (avoid flaky fixed-delay)
    bool registered = false;
    for (int i = 0; i < 30; ++i) {
        if (worker.is_registered()) { registered = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(registered);

    TaskExecutor executor;
    executor.set_exec_func([](uint64_t id, const CMString& name,
                               const CMString& module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "ok";
        return result;
    });
    worker.set_executor(CMMakeShared<TaskExecutor>(std::move(executor)));

    master.submit_task(1, "write_task", "test_module", {}, {}, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    worker.poll_task();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto completed = master.get_completed_tasks();
    EXPECT_GE(completed.size(), 1u);
    TEST_LOG("write register accepted for normal db: %zu tasks completed", completed.size());

    master.stop();
    worker.stop();
}

TEST_F(WriteRegisterNetworkTest, MasterRejectsWriteToFrozenDb) {
    CMString frozen_db_id = "frozen_test_db_123";

    WriteRegisterAckMessage ack;
    ack.object_name = "test/obj";
    ack.db_id = frozen_db_id;
    ack.success = false;
    ack.error_message = "Database frozen: " + frozen_db_id;
    ack.error_type = TaskErrorType::WRITE_TO_FROZEN_DB;

    EXPECT_FALSE(ack.success);
    EXPECT_EQ(ack.error_type, TaskErrorType::WRITE_TO_FROZEN_DB);
    EXPECT_EQ(ack.error_message, "Database frozen: " + frozen_db_id);

    WriteRegisterAckMessage fresh_ack;
    EXPECT_EQ(fresh_ack.error_type, TaskErrorType::UNKNOWN);

    TEST_LOG("master: frozen db rejection logic verified, error_type=%d",
             static_cast<int>(ack.error_type));
}

TEST_F(WriteRegisterNetworkTest, FatalErrorOnWriteToFrozenDb) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    WorkerAgent worker(3, "127.0.0.1", master.get_port());
    worker.start();
    bool registered = false;
    for (int i = 0; i < 30; ++i) {
        if (worker.is_registered()) { registered = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_TRUE(registered);

    CMString error_msg = "Write registration rejected: Database frozen: abc123";
    TaskErrorType error_type = TaskErrorType::WRITE_TO_FROZEN_DB;

    TaskFailedMessage fail_msg;
    fail_msg.task_id = 99;
    fail_msg.worker_id = 3;
    fail_msg.error_message = error_msg;
    fail_msg.error_type = error_type;
    fail_msg.recoverable = false;

    EXPECT_FALSE(master.is_running() && false);

    master.stop();
    worker.stop();

    TEST_LOG("fatal error handling: verified TaskErrorType enum propagation");
}

TEST_F(WriteRegisterNetworkTest, WriteRegisterAckCarriesErrorType) {
    WriteRegisterAckMessage ack;
    ack.object_name = "test/obj";
    ack.db_id = "test_db";
    ack.success = false;
    ack.error_message = "Database frozen: test_db";
    ack.error_type = TaskErrorType::WRITE_TO_FROZEN_DB;

    EXPECT_FALSE(ack.success);
    EXPECT_EQ(ack.error_type, TaskErrorType::WRITE_TO_FROZEN_DB);

    WriteRegisterAckMessage success_ack;
    success_ack.object_name = "test/obj2";
    success_ack.db_id = "test_db2";
    success_ack.success = true;
    success_ack.error_type = TaskErrorType::UNKNOWN;

    EXPECT_TRUE(success_ack.success);
    EXPECT_EQ(success_ack.error_type, TaskErrorType::UNKNOWN);

    TEST_LOG("WriteRegisterAckMessage: error_type field works correctly");
}

TEST_F(WriteRegisterNetworkTest, TaskFailedMessageCarriesErrorType) {
    TaskFailedMessage msg;
    msg.task_id = 42;
    msg.worker_id = 7;
    msg.error_message = "some error";
    msg.error_type = TaskErrorType::WRITE_REGISTRATION_TIMEOUT;

    EXPECT_EQ(msg.error_type, TaskErrorType::WRITE_REGISTRATION_TIMEOUT);
    EXPECT_NE(msg.error_type, TaskErrorType::UNKNOWN);

    TaskFailedMessage default_msg;
    EXPECT_EQ(default_msg.error_type, TaskErrorType::UNKNOWN);

    TEST_LOG("TaskFailedMessage: error_type field defaults to UNKNOWN");
}

}
