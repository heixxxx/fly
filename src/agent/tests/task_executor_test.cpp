#include <gtest/gtest.h>
#include <agent/cpp/task_executor.h>

namespace fly {

TEST(TaskExecutorTest, DefaultExecute) {
    TaskExecutor executor;
    auto result = executor.execute(1, "my_task", "my_module", {});
    
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_EQ(result.status_, TaskExecStatus::SUCCESS);
    EXPECT_EQ(result.output_, "my_module.my_task");
}

TEST(TaskExecutorTest, CustomExecute) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id_ = task_id;
        result.status_ = TaskExecStatus::SUCCESS;
        result.output_ = "executed:" + task_name + ":" + task_module;
        return result;
    });
    
    auto result = executor.execute(42, "compute", "math", {});
    EXPECT_EQ(result.task_id_, 42);
    EXPECT_EQ(result.output_, "executed:compute:math");
}

TEST(TaskExecutorTest, ExecuteWithArgs) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id_ = task_id;
        result.status_ = TaskExecStatus::SUCCESS;
        result.output_ = "args:" + std::to_string(args.size());
        return result;
    });
    
    auto result = executor.execute(1, "task", "mod", {"a", "b", "c"});
    EXPECT_EQ(result.output_, "args:3");
}

TEST(TaskExecutorTest, ExecuteFailure) {
    TaskExecutor executor([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id_ = task_id;
        result.status_ = TaskExecStatus::FAILED;
        result.error_ = "segmentation fault";
        return result;
    });
    
    auto result = executor.execute(1, "bad_task", "mod", {});
    EXPECT_EQ(result.status_, TaskExecStatus::FAILED);
    EXPECT_EQ(result.error_, "segmentation fault");
}

TEST(TaskExecutorTest, IsRunning) {
    TaskExecutor executor;
    EXPECT_FALSE(executor.is_running());
    
    executor.execute(1, "task", "mod", {});
    EXPECT_FALSE(executor.is_running());
}

TEST(TaskExecutorTest, SetExecFunc) {
    TaskExecutor executor;
    
    executor.set_exec_func([](uint64_t task_id, const CMString& task_name,
                              const CMString& task_module, const CMVector<CMString>& args) {
        TaskExecResult result;
        result.task_id_ = task_id;
        result.status_ = TaskExecStatus::SUCCESS;
        result.output_ = "dynamic:" + task_name;
        return result;
    });
    
    auto result = executor.execute(1, "test", "module", {});
    EXPECT_EQ(result.output_, "dynamic:test");
}

}  // namespace fly