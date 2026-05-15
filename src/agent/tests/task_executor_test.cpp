#include <gtest/gtest.h>
#include <agent/cpp/task_executor.h>

namespace fly {

TEST(TaskExecutorTest, DefaultExecute) {
    TaskExecutor executor;
    auto result = executor.execute(1, "my_task", "my_module", {});
    
    EXPECT_EQ(result.task_id, 1);
    EXPECT_EQ(result.status, TaskExecStatus::SUCCESS);
    EXPECT_EQ(result.output, "my_module.my_task");
}

TEST(TaskExecutorTest, CustomExecute) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "executed:" + task_name + ":" + task_module;
        return result;
    });
    
    auto result = executor.execute(42, "compute", "math", {});
    EXPECT_EQ(result.task_id, 42);
    EXPECT_EQ(result.output, "executed:compute:math");
}

TEST(TaskExecutorTest, ExecuteWithArgs) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "args:" + std::to_string(args.size());
        return result;
    });
    
    auto result = executor.execute(1, "task", "mod", {"a", "b", "c"});
    EXPECT_EQ(result.output, "args:3");
}

TEST(TaskExecutorTest, ExecuteFailure) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::FAILED;
        result.error = "segmentation fault";
        return result;
    });
    
    auto result = executor.execute(1, "bad_task", "mod", {});
    EXPECT_EQ(result.status, TaskExecStatus::FAILED);
    EXPECT_EQ(result.error, "segmentation fault");
}

TEST(TaskExecutorTest, IsRunning) {
    TaskExecutor executor;
    EXPECT_FALSE(executor.is_running());
    
    executor.execute(1, "task", "mod", {});
    EXPECT_FALSE(executor.is_running());
}

TEST(TaskExecutorTest, Cancel) {
    TaskExecutor executor;
    executor.cancel();
    EXPECT_FALSE(executor.is_running());
}

TEST(TaskExecutorTest, SetExecFunc) {
    TaskExecutor executor;
    
    executor.set_exec_func([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id = task_id;
        result.status = TaskExecStatus::SUCCESS;
        result.output = "dynamic:" + task_name;
        return result;
    });
    
    auto result = executor.execute(1, "test", "module", {});
    EXPECT_EQ(result.output, "dynamic:test");
}

}  // namespace fly