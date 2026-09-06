#include <gtest/gtest.h>
#include <task/cpp/task_manager.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <algorithm>
#include <latch>
#include <thread>

namespace fly {

// 测试辅助：用提交字段构造 TaskSubmissionSpec，保持测试紧凑。
// caps/timeout/priority 用默认值（调度测试另有覆盖）。
TaskSubmissionSpec mk_spec(const CMString& name,
                           const CMVector<CMString>& inputs = {},
                           const CMVector<CMString>& outputs = {},
                           const CMVector<CMString>& caps = {},
                           float timeout = -1.0f,
                           int priority = 10) {
    TaskSubmissionSpec s;
    s.name_ = name;
    s.inputs_ = inputs;
    s.outputs_ = outputs;
    s.required_capabilities_ = caps;
    s.attribute_timeout_ = timeout;
    s.priority_ = priority;
    return s;
}

TEST(TaskManagerTest, CreateTask) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test_task", {"input/a"}, {"output/b"}), "{}");

    EXPECT_TRUE(manager.has_task(1));
    auto task = manager.get_task(1);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->submission_.name_, "test_task");
    EXPECT_EQ(task->status_, TaskStatus::PENDING);
    EXPECT_EQ(task->submission_.inputs_.size(), 1);
    EXPECT_EQ(task->submission_.outputs_.size(), 1);
    EXPECT_EQ(task->config_, "{}");
}

TEST(TaskManagerTest, UpdateTaskStatus) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");

    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_EQ(manager.get_task(1)->status_, TaskStatus::RUNNING);

    manager.update_task_status(1, TaskStatus::COMPLETED);
    EXPECT_EQ(manager.get_task(1)->status_, TaskStatus::COMPLETED);
}

TEST(TaskManagerTest, SetError) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");
    manager.update_task_status(1, TaskStatus::FAILED);
    manager.set_error(1, "segmentation fault");

    EXPECT_EQ(manager.get_task(1)->error_message_, "segmentation fault");
}

TEST(TaskManagerTest, SetAssignedWorker) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");
    manager.set_assigned_worker(1, 42);

    EXPECT_EQ(manager.get_task(1)->assigned_worker_id_, 42);
}

TEST(TaskManagerTest, SetTimestamps) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");
    manager.set_timestamps(1, 100, 200, 300);

    auto task = manager.get_task(1);
    ASSERT_NE(task, nullptr);
    EXPECT_EQ(task->created_at_, 100);
    EXPECT_EQ(task->started_at_, 200);
    EXPECT_EQ(task->completed_at_, 300);
}

TEST(TaskManagerTest, GetTasksByStatus) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");
    manager.create_task(3, mk_spec("task3"), "");

    manager.update_task_status(1, TaskStatus::COMPLETED);
    manager.update_task_status(2, TaskStatus::RUNNING);

    auto completed = manager.get_tasks_by_status(TaskStatus::COMPLETED);
    EXPECT_EQ(completed.size(), 1);
    EXPECT_EQ(completed[0]->task_id_, 1);

    auto running = manager.get_tasks_by_status(TaskStatus::RUNNING);
    EXPECT_EQ(running.size(), 1);
    EXPECT_EQ(running[0]->task_id_, 2);

    auto pending = manager.get_tasks_by_status(TaskStatus::PENDING);
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0]->task_id_, 3);
}

TEST(TaskManagerTest, GetAllTasks) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");

    auto all = manager.get_all_tasks();
    EXPECT_EQ(all.size(), 2);
}

TEST(TaskManagerTest, RemoveTask) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");
    manager.remove_task(1);

    EXPECT_FALSE(manager.has_task(1));
    EXPECT_EQ(manager.get_task(1), nullptr);
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
    manager.create_task(1, mk_spec("test"), "");
    manager.set_timestamps(1, 100, 200, 300);

    auto task = manager.get_task(1);
    EXPECT_EQ(task->created_at_, 100);
    EXPECT_EQ(task->started_at_, 200);
    EXPECT_EQ(task->completed_at_, 300);

    manager.set_timestamps(1, 0, 0, 0);
    task = manager.get_task(1);
    EXPECT_EQ(task->created_at_, 100);
    EXPECT_EQ(task->started_at_, 200);
    EXPECT_EQ(task->completed_at_, 300);

    manager.set_timestamps(1, 500, 0, 600);
    task = manager.get_task(1);
    EXPECT_EQ(task->created_at_, 500);
    EXPECT_EQ(task->started_at_, 200);
    EXPECT_EQ(task->completed_at_, 600);
}

// create_task 对已存在的 task_id 会 assert 崩溃（语义已从"隐式覆盖"改为"严格新建"）。
// rerun 失败 task 必须先 remove_task 再 create_task，正确范式见 RemoveTaskThenRecreate。
// 原 CreateTaskOverwritesExisting 测试验证的是已移除的 erase 覆盖语义，故删除。

TEST(TaskManagerTest, GetTaskNonExistent) {
    TaskManager manager;
    EXPECT_EQ(manager.get_task(999), nullptr);
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
    manager.create_task(1, mk_spec("test"), "");
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
    manager.create_task(1, mk_spec("test"), "");
    EXPECT_EQ(manager.get_task(1)->started_at_, 0);

    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_GT(manager.get_task(1)->started_at_, 0);

    manager.update_task_status(1, TaskStatus::COMPLETED);
    EXPECT_GT(manager.get_task(1)->completed_at_, 0);
}

TEST(TaskManagerTest, TaskWithCapabilities) {
    TaskManager manager;
    manager.create_task(1, mk_spec("gpu_task", {}, {}, {"gpu", "cuda"}), "{}");
    auto task = manager.get_task(1);
    EXPECT_EQ(task->submission_.required_capabilities_.size(), 2);
    EXPECT_EQ(task->submission_.required_capabilities_[0], "gpu");
    EXPECT_EQ(task->submission_.required_capabilities_[1], "cuda");
}

TEST(TaskManagerTest, GetTasksByStatusMultipleStatuses) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");
    manager.create_task(3, mk_spec("task3"), "");
    manager.create_task(4, mk_spec("task4"), "");

    manager.update_task_status(1, TaskStatus::RUNNING);
    manager.update_task_status(2, TaskStatus::COMPLETED);
    manager.update_task_status(3, TaskStatus::FAILED);

    auto running = manager.get_tasks_by_status(TaskStatus::RUNNING);
    EXPECT_EQ(running.size(), 1);
    EXPECT_EQ(running[0]->task_id_, 1);

    auto completed = manager.get_tasks_by_status(TaskStatus::COMPLETED);
    EXPECT_EQ(completed.size(), 1);
    EXPECT_EQ(completed[0]->task_id_, 2);

    auto failed = manager.get_tasks_by_status(TaskStatus::FAILED);
    EXPECT_EQ(failed.size(), 1);
    EXPECT_EQ(failed[0]->task_id_, 3);

    auto pending = manager.get_tasks_by_status(TaskStatus::PENDING);
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0]->task_id_, 4);
}

TEST(TaskManagerTest, GetAllTasksMultiple) {
    TaskManager manager;
    manager.create_task(10, mk_spec("t10"), "");
    manager.create_task(20, mk_spec("t20"), "");
    manager.create_task(30, mk_spec("t30"), "");

    auto all = manager.get_all_tasks();
    EXPECT_EQ(all.size(), 3);
}

TEST(TaskManagerTest, SetErrorOverwritesPrevious) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");

    manager.set_error(1, "first error");
    EXPECT_EQ(manager.get_task(1)->error_message_, "first error");

    manager.set_error(1, "second error");
    EXPECT_EQ(manager.get_task(1)->error_message_, "second error");
}

TEST(TaskManagerTest, SetAssignedWorkerOverwrites) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");

    manager.set_assigned_worker(1, 42);
    EXPECT_EQ(manager.get_task(1)->assigned_worker_id_, 42);

    manager.set_assigned_worker(1, 99);
    EXPECT_EQ(manager.get_task(1)->assigned_worker_id_, 99);
}

TEST(TaskManagerTest, UpdateTaskStatusRunningSetsStartedAtOnce) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");
    EXPECT_EQ(manager.get_task(1)->started_at_, 0);

    manager.update_task_status(1, TaskStatus::RUNNING);
    uint64_t first_started = manager.get_task(1)->started_at_;
    EXPECT_GT(first_started, 0);

    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_EQ(manager.get_task(1)->started_at_, first_started);
}

TEST(TaskManagerTest, UpdateTaskStatusFailedSetsCompletedAt) {
    TaskManager manager;
    manager.create_task(1, mk_spec("test"), "");

    manager.update_task_status(1, TaskStatus::FAILED);
    EXPECT_GT(manager.get_task(1)->completed_at_, 0);
    EXPECT_EQ(manager.get_task(1)->status_, TaskStatus::FAILED);
}

TEST(TaskManagerTest, CreateTaskInitializesFields) {
    TaskManager manager;
    CMVector<CMString> inputs = {"input/a", "input/b"};
    CMVector<CMString> outputs = {"output/c"};
    manager.create_task(42, mk_spec("my_task", inputs, outputs), "{\"key\":\"val\"}");

    auto task = manager.get_task(42);
    EXPECT_EQ(task->task_id_, 42);
    EXPECT_EQ(task->submission_.name_, "my_task");
    EXPECT_EQ(task->status_, TaskStatus::PENDING);
    EXPECT_EQ(task->submission_.inputs_.size(), 2);
    EXPECT_EQ(task->submission_.outputs_.size(), 1);
    EXPECT_EQ(task->config_, "{\"key\":\"val\"}");
    EXPECT_EQ(task->assigned_worker_id_, 0);
    EXPECT_TRUE(task->error_message_.empty());
}

TEST(TaskManagerTest, RemoveTaskThenRecreate) {
    TaskManager manager;
    manager.create_task(1, mk_spec("first"), "");
    manager.remove_task(1);
    EXPECT_FALSE(manager.has_task(1));

    manager.create_task(1, mk_spec("second"), "");
    EXPECT_TRUE(manager.has_task(1));
    EXPECT_EQ(manager.get_task(1)->submission_.name_, "second");
}

// ── New tests for optimized API ────────────────────────────────────

TEST(TaskManagerTest, HasTasksWithStatus) {
    TaskManager manager;
    EXPECT_FALSE(manager.has_tasks_with_status(TaskStatus::RUNNING));

    manager.create_task(1, mk_spec("task1"), "");
    EXPECT_TRUE(manager.has_tasks_with_status(TaskStatus::PENDING));
    EXPECT_FALSE(manager.has_tasks_with_status(TaskStatus::RUNNING));

    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_FALSE(manager.has_tasks_with_status(TaskStatus::PENDING));
    EXPECT_TRUE(manager.has_tasks_with_status(TaskStatus::RUNNING));
}

TEST(TaskManagerTest, CountTasksByStatus) {
    TaskManager manager;
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 0);

    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");
    manager.create_task(3, mk_spec("task3"), "");
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 3);

    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 2);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::RUNNING), 1);

    manager.update_task_status(2, TaskStatus::COMPLETED);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 1);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::COMPLETED), 1);
}

TEST(TaskManagerTest, FailTask) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");

    manager.fail_task(1, "out of memory");
    auto task = manager.get_task(1);
    EXPECT_EQ(task->status_, TaskStatus::FAILED);
    EXPECT_EQ(task->error_message_, "out of memory");
    EXPECT_GT(task->completed_at_, 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::FAILED), 1);
}

TEST(TaskManagerTest, FailTaskAlreadyFailed) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.fail_task(1, "first error");
    manager.fail_task(1, "second error");

    auto task = manager.get_task(1);
    EXPECT_EQ(task->status_, TaskStatus::FAILED);
    EXPECT_EQ(task->error_message_, "second error");
}

TEST(TaskManagerTest, AssignTask) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");

    manager.assign_task(1, 42);
    auto task = manager.get_task(1);
    EXPECT_EQ(task->status_, TaskStatus::RUNNING);
    EXPECT_EQ(task->assigned_worker_id_, 42);
    EXPECT_GT(task->started_at_, 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::RUNNING), 1);
}

TEST(TaskManagerTest, UnassignTask) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.assign_task(1, 42);
    EXPECT_EQ(manager.get_task(1)->status_, TaskStatus::RUNNING);

    manager.unassign_task(1);
    auto task = manager.get_task(1);
    EXPECT_EQ(task->status_, TaskStatus::PENDING);
    EXPECT_EQ(task->assigned_worker_id_, 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 1);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::RUNNING), 0);
}

TEST(TaskManagerTest, GetTaskIdsByStatus) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");
    manager.create_task(3, mk_spec("task3"), "");

    manager.update_task_status(1, TaskStatus::RUNNING);
    manager.update_task_status(2, TaskStatus::RUNNING);

    auto running_ids = manager.get_task_ids_by_status(TaskStatus::RUNNING);
    EXPECT_EQ(running_ids.size(), 2);

    auto pending_ids = manager.get_task_ids_by_status(TaskStatus::PENDING);
    EXPECT_EQ(pending_ids.size(), 1);
    EXPECT_EQ(pending_ids[0], 3);
}

TEST(TaskManagerTest, GetTaskIdsByWorker) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");
    manager.create_task(3, mk_spec("task3"), "");

    manager.assign_task(1, 42);
    manager.assign_task(2, 42);
    manager.assign_task(3, 99);

    auto worker42_ids = manager.get_task_ids_by_worker(42);
    EXPECT_EQ(worker42_ids.size(), 2);

    auto worker99_ids = manager.get_task_ids_by_worker(99);
    EXPECT_EQ(worker99_ids.size(), 1);
    EXPECT_EQ(worker99_ids[0], 3);

    auto worker_none = manager.get_task_ids_by_worker(0);
    EXPECT_EQ(worker_none.size(), 0);
}

TEST(TaskManagerTest, SetWriteContextHash) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");

    manager.set_write_context_hash(1, "abc123");
    EXPECT_EQ(manager.get_task(1)->submission_.write_context_hash_, "abc123");

    manager.set_write_context_hash(1, "def456");
    EXPECT_EQ(manager.get_task(1)->submission_.write_context_hash_, "def456");
}

TEST(TaskManagerTest, StatusCountsAfterRemove) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");
    manager.update_task_status(1, TaskStatus::RUNNING);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::RUNNING), 1);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 1);

    manager.remove_task(1);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::RUNNING), 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 1);

    manager.remove_task(2);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 0);
}

TEST(TaskManagerTest, AutoCleanupCompletedTasks) {
    TaskManager manager;
    for (int i = 0; i < kMaxCompletedTasks + 10; i++) {
        manager.create_task(i, mk_spec("task"), "");
        manager.update_task_status(i, TaskStatus::COMPLETED);
    }
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::COMPLETED), kMaxCompletedTasks);
}

TEST(TaskManagerTest, CompoundOperationsNonExistent) {
    TaskManager manager;
    EXPECT_NO_THROW(manager.fail_task(999, "error"));
    EXPECT_NO_THROW(manager.assign_task(999, 42));
    EXPECT_NO_THROW(manager.unassign_task(999));
    EXPECT_NO_THROW(manager.set_write_context_hash(999, "hash"));
}

TEST(TaskManagerTest, SharedPtrLifetime) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    auto task = manager.get_task(1);
    ASSERT_NE(task, nullptr);

    // Remove task — shared_ptr should still be valid.
    manager.remove_task(1);
    EXPECT_EQ(task->submission_.name_, "task1");
    EXPECT_EQ(manager.get_task(1), nullptr);
}

TEST(TaskManagerTest, GetTasksByStatusReturnsSharedPtrs) {
    TaskManager manager;
    manager.create_task(1, mk_spec("task1"), "");
    manager.create_task(2, mk_spec("task2"), "");
    manager.update_task_status(1, TaskStatus::RUNNING);

    auto running = manager.get_tasks_by_status(TaskStatus::RUNNING);
    EXPECT_EQ(running.size(), 1);
    EXPECT_NE(running[0], nullptr);
    EXPECT_EQ(running[0]->task_id_, 1);

    // Modify via shared_ptr — visible to all holders.
    running[0]->submission_.name_ = "modified";
    EXPECT_EQ(manager.get_task(1)->submission_.name_, "modified");
}

// TaskSubmissionSpec 序列化往返：验证嵌套 struct 经 FLY_SERIALIZE 整体序列化
// 后字段不丢。这是 FailedTaskRecord 持久化（落盘/restart）的基础——若 spec 的
// FLY_SERIALIZE 漏加字段，磁盘读回会取默认值（priority bug 的原始形态）。
TEST(TaskSubmissionSpecTest, SerializeRoundTripPreservesAllFields) {
    TaskSubmissionSpec original;
    original.name_ = "my_task";
    original.module_ = "my_module";
    original.args_ = {"arg1", "arg2"};
    original.inputs_ = {"in1", "in2"};
    original.outputs_ = {"out1"};
    original.required_capabilities_ = {"gpu", "cuda"};
    original.attribute_timeout_ = 5.0f;
    original.priority_ = 20;
    original.write_context_hash_ = "abc123";
    original.vars_ = {"db:var1", "db:var2"};

    // 序列化
    CMString encoded;
    FLY_ENCODE(original, encoded);

    // 反序列化
    TaskSubmissionSpec decoded;
    FLY_DECODE(encoded, TaskSubmissionSpec, decoded);

    EXPECT_EQ(decoded.name_, "my_task");
    EXPECT_EQ(decoded.module_, "my_module");
    EXPECT_EQ(decoded.args_.size(), 2u);
    EXPECT_EQ(decoded.inputs_.size(), 2u);
    EXPECT_EQ(decoded.outputs_.size(), 1u);
    EXPECT_EQ(decoded.required_capabilities_.size(), 2u);
    EXPECT_EQ(decoded.required_capabilities_[0], "gpu");
    EXPECT_FLOAT_EQ(decoded.attribute_timeout_, 5.0f);
    EXPECT_EQ(decoded.priority_, 20);
    EXPECT_EQ(decoded.write_context_hash_, "abc123");
    EXPECT_EQ(decoded.vars_.size(), 2u);
}

// 默认构造的 spec 应保留默认值（priority=10, attribute_timeout=-1.0 死等）。
// 验证向后兼容：未显式设值的字段不会因序列化往返变成垃圾值。
TEST(TaskSubmissionSpecTest, DefaultValuesSurviveRoundTrip) {
    TaskSubmissionSpec original;  // 全默认

    CMString encoded;
    FLY_ENCODE(original, encoded);
    TaskSubmissionSpec decoded;
    FLY_DECODE(encoded, TaskSubmissionSpec, decoded);

    EXPECT_EQ(decoded.priority_, 10);
    EXPECT_FLOAT_EQ(decoded.attribute_timeout_, -1.0f);
    EXPECT_TRUE(decoded.args_.empty());
    EXPECT_TRUE(decoded.vars_.empty());
}

// ── 并发正确性（P3-17 批 1）：4 线程分段 create + 状态推进，读者 hammer
//    计数口径——join 后按状态计数守恒（全量 COMPLETE，无丢无重）。 ──

TEST(TaskManagerTest, ConcurrentCreateAdvanceCountsConserve) {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 150;
    TaskManager manager;
    std::latch go{kThreads + 2};
    CMVector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            go.count_down(); go.wait();
            for (int i = 0; i < kPerThread; ++i) {
                uint64_t id = static_cast<uint64_t>(t * kPerThread + i + 1);
                manager.create_task(id, mk_spec("conc_task"), "");
                manager.assign_task(id, static_cast<uint64_t>(t + 1));
                manager.update_task_status(id, TaskStatus::RUNNING);
                manager.update_task_status(id, TaskStatus::COMPLETED);
            }
        });
    }
    for (int r = 0; r < 2; ++r) {
        threads.emplace_back([&] {
            go.count_down(); go.wait();
            for (int c = 0; c < 200; ++c) {
                (void)manager.count_tasks_by_status(TaskStatus::RUNNING);
                (void)manager.get_task_ids_by_worker(1);
                (void)manager.get_all_tasks();
            }
        });
    }
    for (auto& th : threads) th.join();

    // 完成上限裁剪不变量（kMaxCompletedTasks=100，并发裁剪下仍精确）：
    // COMPLETED 计数恰为上限，其余状态清零。
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::COMPLETED),
              std::min(kThreads * kPerThread, 100));
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::RUNNING), 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::PENDING), 0);
    EXPECT_EQ(manager.count_tasks_by_status(TaskStatus::FAILED), 0);
}

}  // namespace fly
