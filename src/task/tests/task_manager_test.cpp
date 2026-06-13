#include <gtest/gtest.h>
#include <task/cpp/task_manager.h>

namespace fly {

TEST(TaskManagerTest, CreateTask) {
    TaskManager manager;
    manager.create_task(1, "test_task", {"input/a"}, {"output/b"}, "{}");
    
    EXPECT_TRUE(manager.has_task(1));
    auto task_opt = manager.get_task(1);
    ASSERT_TRUE(task_opt.has_value());
    auto& task = task_opt->get();
    EXPECT_EQ(task.name_, "test_task");
    EXPECT_EQ(task.status_, TaskStatus::PENDING);
    EXPECT_EQ(task.inputs_.size(), 1);
    EXPECT_EQ(task.outputs_.size(), 1);
    EXPECT_EQ(task.config_, "{}");
}

TEST(TaskManagerTest, UpdateTaskStatus) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    
    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_EQ(manager.get_task(1)->get().status_, TaskStatus::RUNNING);
    
    manager.update_task_status(1, TaskStatus::COMPLETED);
    EXPECT_EQ(manager.get_task(1)->get().status_, TaskStatus::COMPLETED);
}

TEST(TaskManagerTest, SetError) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.update_task_status(1, TaskStatus::FAILED);
    manager.set_error(1, "segmentation fault");
    
    EXPECT_EQ(manager.get_task(1)->get().error_message_, "segmentation fault");
}

TEST(TaskManagerTest, SetAssignedWorker) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.set_assigned_worker(1, 42);
    
    EXPECT_EQ(manager.get_task(1)->get().assigned_worker_id_, 42);
}

TEST(TaskManagerTest, SetTimestamps) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.set_timestamps(1, 100, 200, 300);
    
    auto task_opt2 = manager.get_task(1);
    ASSERT_TRUE(task_opt2.has_value());
    auto& task2 = task_opt2->get();
    EXPECT_EQ(task2.created_at_, 100);
    EXPECT_EQ(task2.started_at_, 200);
    EXPECT_EQ(task2.completed_at_, 300);
}

TEST(TaskManagerTest, GetTasksByStatus) {
    TaskManager manager;
    manager.create_task(1, "task1", {}, {}, "");
    manager.create_task(2, "task2", {}, {}, "");
    manager.create_task(3, "task3", {}, {}, "");
    
    manager.update_task_status(1, TaskStatus::COMPLETED);
    manager.update_task_status(2, TaskStatus::RUNNING);
    
    auto completed = manager.get_tasks_by_status(TaskStatus::COMPLETED);
    EXPECT_EQ(completed.size(), 1);
    EXPECT_EQ(completed[0].task_id_, 1);
    
    auto running = manager.get_tasks_by_status(TaskStatus::RUNNING);
    EXPECT_EQ(running.size(), 1);
    EXPECT_EQ(running[0].task_id_, 2);
    
    auto pending = manager.get_tasks_by_status(TaskStatus::PENDING);
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].task_id_, 3);
}

TEST(TaskManagerTest, GetAllTasks) {
    TaskManager manager;
    manager.create_task(1, "task1", {}, {}, "");
    manager.create_task(2, "task2", {}, {}, "");
    
    auto all = manager.get_all_tasks();
    EXPECT_EQ(all.size(), 2);
}

TEST(TaskManagerTest, RemoveTask) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.remove_task(1);
    
    EXPECT_FALSE(manager.has_task(1));
    EXPECT_FALSE(manager.get_task(1).has_value());
}

TEST(TaskManagerTest, UpdateTaskStatusNonExistent) {
    TaskManager manager;
    EXPECT_NO_THROW(manager.update_task_status(999, TaskStatus::RUNNING));
    EXPECT_FALSE(manager.has_task(999));
}

TEST(TaskManagerTest, SetErrorNonExistent) {
    TaskManager manager;
    EXPECT_NO_THROW(manager.set_error(999, "no error"));
}

TEST(TaskManagerTest, SetAssignedWorkerNonExistent) {
    TaskManager manager;
    EXPECT_NO_THROW(manager.set_assigned_worker(999, 42));
}

TEST(TaskManagerTest, SetTimestampsNonExistent) {
    TaskManager manager;
    EXPECT_NO_THROW(manager.set_timestamps(999, 100, 200, 300));
}

TEST(TaskManagerTest, SetTimestampsWithZeroValuesSkips) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.set_timestamps(1, 100, 200, 300);

    auto& task = manager.get_task(1)->get();
    EXPECT_EQ(task.created_at_, 100);
    EXPECT_EQ(task.started_at_, 200);
    EXPECT_EQ(task.completed_at_, 300);

    manager.set_timestamps(1, 0, 0, 0);
    EXPECT_EQ(task.created_at_, 100);
    EXPECT_EQ(task.started_at_, 200);
    EXPECT_EQ(task.completed_at_, 300);

    manager.set_timestamps(1, 500, 0, 600);
    EXPECT_EQ(task.created_at_, 500);
    EXPECT_EQ(task.started_at_, 200);
    EXPECT_EQ(task.completed_at_, 600);
}

TEST(TaskManagerTest, CreateTaskOverwritesExisting) {
    TaskManager manager;
    manager.create_task(1, "first", {}, {}, "");
    manager.create_task(1, "second", {"input/a"}, {"output/b"}, "{}");

    auto& task = manager.get_task(1)->get();
    EXPECT_EQ(task.name_, "second");
    EXPECT_EQ(task.inputs_.size(), 1);
    EXPECT_EQ(task.outputs_.size(), 1);
}

TEST(TaskManagerTest, GetTaskNonExistent) {
    TaskManager manager;
    EXPECT_FALSE(manager.get_task(999).has_value());
}

TEST(TaskManagerTest, HasTaskReturnsFalseForMissing) {
    TaskManager manager;
    EXPECT_FALSE(manager.has_task(999));
}

TEST(TaskManagerTest, RemoveTaskNonExistent) {
    TaskManager manager;
    EXPECT_NO_THROW(manager.remove_task(999));
}

TEST(TaskManagerTest, GetTasksByStatusEmptyWhenNoneMatch) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.update_task_status(1, TaskStatus::COMPLETED);

    auto running = manager.get_tasks_by_status(TaskStatus::RUNNING);
    EXPECT_TRUE(running.empty());

    auto failed = manager.get_tasks_by_status(TaskStatus::FAILED);
    EXPECT_TRUE(failed.empty());
}

TEST(TaskManagerTest, GetAllTasksEmpty) {
    TaskManager manager;
    auto all = manager.get_all_tasks();
    EXPECT_TRUE(all.empty());
}

TEST(TaskManagerTest, UpdateTaskStatusAutoTimestamps) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    EXPECT_EQ(manager.get_task(1)->get().started_at_, 0);

    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_GT(manager.get_task(1)->get().started_at_, 0);

    manager.update_task_status(1, TaskStatus::COMPLETED);
    EXPECT_GT(manager.get_task(1)->get().completed_at_, 0);
}

TEST(TaskManagerTest, TaskWithCapabilities) {
    TaskManager manager;
    manager.create_task(1, "gpu_task", {}, {}, "{}", {"gpu", "cuda"});
    auto& task = manager.get_task(1)->get();
    EXPECT_EQ(task.required_capabilities_.size(), 2);
    EXPECT_EQ(task.required_capabilities_[0], "gpu");
    EXPECT_EQ(task.required_capabilities_[1], "cuda");
}

TEST(TaskManagerTest, GetTasksByStatusMultipleStatuses) {
    TaskManager manager;
    manager.create_task(1, "task1", {}, {}, "");
    manager.create_task(2, "task2", {}, {}, "");
    manager.create_task(3, "task3", {}, {}, "");
    manager.create_task(4, "task4", {}, {}, "");

    manager.update_task_status(1, TaskStatus::RUNNING);
    manager.update_task_status(2, TaskStatus::COMPLETED);
    manager.update_task_status(3, TaskStatus::FAILED);

    auto running = manager.get_tasks_by_status(TaskStatus::RUNNING);
    EXPECT_EQ(running.size(), 1);
    EXPECT_EQ(running[0].task_id_, 1);

    auto completed = manager.get_tasks_by_status(TaskStatus::COMPLETED);
    EXPECT_EQ(completed.size(), 1);
    EXPECT_EQ(completed[0].task_id_, 2);

    auto failed = manager.get_tasks_by_status(TaskStatus::FAILED);
    EXPECT_EQ(failed.size(), 1);
    EXPECT_EQ(failed[0].task_id_, 3);

    auto pending = manager.get_tasks_by_status(TaskStatus::PENDING);
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].task_id_, 4);
}

TEST(TaskManagerTest, GetAllTasksMultiple) {
    TaskManager manager;
    manager.create_task(10, "t10", {}, {}, "");
    manager.create_task(20, "t20", {}, {}, "");
    manager.create_task(30, "t30", {}, {}, "");

    auto all = manager.get_all_tasks();
    EXPECT_EQ(all.size(), 3);
}

TEST(TaskManagerTest, SetErrorOverwritesPrevious) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");

    manager.set_error(1, "first error");
    EXPECT_EQ(manager.get_task(1)->get().error_message_, "first error");

    manager.set_error(1, "second error");
    EXPECT_EQ(manager.get_task(1)->get().error_message_, "second error");
}

TEST(TaskManagerTest, SetAssignedWorkerOverwrites) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");

    manager.set_assigned_worker(1, 42);
    EXPECT_EQ(manager.get_task(1)->get().assigned_worker_id_, 42);

    manager.set_assigned_worker(1, 99);
    EXPECT_EQ(manager.get_task(1)->get().assigned_worker_id_, 99);
}

TEST(TaskManagerTest, UpdateTaskStatusRunningSetsStartedAtOnce) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    EXPECT_EQ(manager.get_task(1)->get().started_at_, 0);

    manager.update_task_status(1, TaskStatus::RUNNING);
    uint64_t first_started = manager.get_task(1)->get().started_at_;
    EXPECT_GT(first_started, 0);

    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_EQ(manager.get_task(1)->get().started_at_, first_started);
}

TEST(TaskManagerTest, UpdateTaskStatusFailedSetsCompletedAt) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");

    manager.update_task_status(1, TaskStatus::FAILED);
    EXPECT_GT(manager.get_task(1)->get().completed_at_, 0);
    EXPECT_EQ(manager.get_task(1)->get().status_, TaskStatus::FAILED);
}

TEST(TaskManagerTest, CreateTaskInitializesFields) {
    TaskManager manager;
    CMVector<CMString> inputs = {"input/a", "input/b"};
    CMVector<CMString> outputs = {"output/c"};
    manager.create_task(42, "my_task", inputs, outputs, "{\"key\":\"val\"}");

    auto& task = manager.get_task(42)->get();
    EXPECT_EQ(task.task_id_, 42);
    EXPECT_EQ(task.name_, "my_task");
    EXPECT_EQ(task.status_, TaskStatus::PENDING);
    EXPECT_EQ(task.inputs_.size(), 2);
    EXPECT_EQ(task.outputs_.size(), 1);
    EXPECT_EQ(task.config_, "{\"key\":\"val\"}");
    EXPECT_EQ(task.assigned_worker_id_, 0);
    EXPECT_TRUE(task.error_message_.empty());
}

TEST(TaskManagerTest, RemoveTaskThenRecreate) {
    TaskManager manager;
    manager.create_task(1, "first", {}, {}, "");
    manager.remove_task(1);
    EXPECT_FALSE(manager.has_task(1));

    manager.create_task(1, "second", {}, {}, "");
    EXPECT_TRUE(manager.has_task(1));
    EXPECT_EQ(manager.get_task(1)->get().name_, "second");
}

}  // namespace fly
