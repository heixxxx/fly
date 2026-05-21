#include <gtest/gtest.h>
#include <task/cpp/task_manager.h>

namespace fly {

TEST(TaskManagerTest, CreateTask) {
    TaskManager manager;
    manager.create_task(1, "test_task", {"input/a"}, {"output/b"}, "{}");
    
    EXPECT_TRUE(manager.has_task(1));
    auto* task = manager.get_task(1);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->name, "test_task");
    EXPECT_EQ(task->status, TaskStatus::PENDING);
    EXPECT_EQ(task->inputs.size(), 1);
    EXPECT_EQ(task->outputs.size(), 1);
    EXPECT_EQ(task->config, "{}");
}

TEST(TaskManagerTest, UpdateTaskStatus) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    
    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_EQ(manager.get_task(1)->status, TaskStatus::RUNNING);
    
    manager.update_task_status(1, TaskStatus::COMPLETED);
    EXPECT_EQ(manager.get_task(1)->status, TaskStatus::COMPLETED);
}

TEST(TaskManagerTest, SetError) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.update_task_status(1, TaskStatus::FAILED);
    manager.set_error(1, "segmentation fault");
    
    EXPECT_EQ(manager.get_task(1)->error_message, "segmentation fault");
}

TEST(TaskManagerTest, SetAssignedWorker) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.set_assigned_worker(1, 42);
    
    EXPECT_EQ(manager.get_task(1)->assigned_worker_id, 42);
}

TEST(TaskManagerTest, SetTimestamps) {
    TaskManager manager;
    manager.create_task(1, "test", {}, {}, "");
    manager.set_timestamps(1, 100, 200, 300);
    
    auto* task = manager.get_task(1);
    EXPECT_EQ(task->created_at, 100);
    EXPECT_EQ(task->started_at, 200);
    EXPECT_EQ(task->completed_at, 300);
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
    EXPECT_EQ(completed[0].task_id, 1);
    
    auto running = manager.get_tasks_by_status(TaskStatus::RUNNING);
    EXPECT_EQ(running.size(), 1);
    EXPECT_EQ(running[0].task_id, 2);
    
    auto pending = manager.get_tasks_by_status(TaskStatus::PENDING);
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].task_id, 3);
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
    EXPECT_EQ(manager.get_task(1), nullptr);
}

}  // namespace fly