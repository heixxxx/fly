#include <gtest/gtest.h>
#include <task/cpp/dependency_graph.h>
#include <latch>
#include <thread>

namespace fly {

// 构造仅含 capabilities 的 TaskRequirements（timeout 默认 <0 即死等，等价旧语义）
static TaskRequirements caps(CMVector<CMString> c) {
    TaskRequirements r;
    r.capabilities_ = std::move(c);
    return r;
}

TEST(DependencyGraphTest, AddTaskWithNoDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {});
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
}

TEST(DependencyGraphTest, AddTaskWithDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    graph.add_task(2, {"input/b"});

    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 0);

    graph.mark_data_ready("input/a");
    ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);
}

TEST(DependencyGraphTest, MultipleDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a", "input/b"});

    graph.mark_data_ready("input/a");
    EXPECT_FALSE(graph.is_task_ready(1));

    graph.mark_data_ready("input/b");
    EXPECT_TRUE(graph.is_task_ready(1));

    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
}

TEST(DependencyGraphTest, RemoveTask) {
    DependencyGraph graph;
    graph.add_task(1, {});
    graph.remove_task(1);
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 0);
}

TEST(DependencyGraphTest, CascadingDependencies) {
    DependencyGraph graph;
    graph.add_task(1, {});
    graph.add_task(2, {"output/1"});
    graph.add_task(3, {"output/2"});

    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 1);

    graph.remove_task(1);
    graph.mark_data_ready("output/1");

    ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0], 2);
}

TEST(DependencyGraphTest, AddTaskWithRequirements) {
    DependencyGraph graph;
    graph.add_task(1, {}, caps({"gpu", "cuda"}));

    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_EQ(reqs.capabilities_.size(), 2u);
    EXPECT_EQ(reqs.capabilities_[0], "gpu");
    EXPECT_EQ(reqs.capabilities_[1], "cuda");
}

TEST(DependencyGraphTest, GetRequirementsNonExistent) {
    DependencyGraph graph;
    const auto& reqs = graph.get_task_requirements(999);
    EXPECT_TRUE(reqs.capabilities_.empty());
}

TEST(DependencyGraphTest, RequirementsClearedOnRemove) {
    DependencyGraph graph;
    graph.add_task(1, {}, caps({"gpu"}));
    graph.remove_task(1);

    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_TRUE(reqs.capabilities_.empty());
}

TEST(DependencyGraphTest, NoRequirementsDefault) {
    DependencyGraph graph;
    graph.add_task(1, {});

    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_TRUE(reqs.capabilities_.empty());
}

TEST(DependencyGraphTest, MarkDataRemovedMovesReadyTaskBackToPending) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);

    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_removed("input/a");
    EXPECT_FALSE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);

    auto pending = graph.get_pending_tasks();
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0], 1);
}

TEST(DependencyGraphTest, MarkDataRemovedNoOpWhenNoReadyTasks) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);

    graph.mark_data_removed("input/a");
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);
    EXPECT_EQ(graph.get_pending_tasks().size(), 1);
}

TEST(DependencyGraphTest, MarkDataRemovedNoOpWhenNoTaskDependsOnIt) {
    DependencyGraph graph;
    graph.add_task(1, {});
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_removed("unrelated/path");
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
    EXPECT_TRUE(graph.is_task_ready(1));
}

TEST(DependencyGraphTest, MarkDataRemovedPartialDeps) {
    DependencyGraph graph;
    graph.add_task(1, {"dep/a", "dep/b"});
    EXPECT_FALSE(graph.is_task_ready(1));

    graph.mark_data_ready("dep/a");
    EXPECT_FALSE(graph.is_task_ready(1));

    graph.mark_data_ready("dep/b");
    EXPECT_TRUE(graph.is_task_ready(1));

    graph.mark_data_removed("dep/a");
    EXPECT_FALSE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_pending_tasks().size(), 1);
}

TEST(DependencyGraphTest, MarkDataRemovedMultipleTasks) {
    DependencyGraph graph;
    graph.add_task(1, {"shared/input"});
    graph.add_task(2, {"shared/input"});
    graph.add_task(3, {});

    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_ready("shared/input");
    EXPECT_EQ(graph.get_ready_tasks().size(), 3);

    graph.mark_data_removed("shared/input");
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
    EXPECT_TRUE(graph.is_task_ready(3));
    EXPECT_EQ(graph.get_pending_tasks().size(), 2);
}

TEST(DependencyGraphTest, MarkDataRemovedThenReadyAgain) {
    DependencyGraph graph;
    graph.add_task(1, {"input/x"});

    graph.mark_data_ready("input/x");
    EXPECT_TRUE(graph.is_task_ready(1));

    graph.mark_data_removed("input/x");
    EXPECT_FALSE(graph.is_task_ready(1));

    graph.mark_data_ready("input/x");
    EXPECT_TRUE(graph.is_task_ready(1));
}

TEST(DependencyGraphTest, MarkDataRemovedNonExistentPath) {
    DependencyGraph graph;
    graph.add_task(1, {});
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);

    graph.mark_data_removed("never/existed");
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
}

TEST(DependencyGraphTest, MarkDataReadyIdempotent) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});

    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.is_task_ready(1));

    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.is_task_ready(1));
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
}

TEST(DependencyGraphTest, IsDataReadyForMissingPath) {
    DependencyGraph graph;
    EXPECT_FALSE(graph.is_data_ready("never/added"));
}

TEST(DependencyGraphTest, IsTaskReadyForNonExistentTask) {
    DependencyGraph graph;
    EXPECT_FALSE(graph.is_task_ready(999));
}

TEST(DependencyGraphTest, RemoveTaskNonExistent) {
    DependencyGraph graph;
    EXPECT_NO_THROW(graph.remove_task(999));
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);
    EXPECT_EQ(graph.get_pending_tasks().size(), 0);
}

TEST(DependencyGraphTest, GetTaskDependenciesForExistingTask) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a", "input/b"}, caps({"gpu"}));
    auto deps = graph.get_task_dependencies(1);
    EXPECT_EQ(deps.size(), 2);
    EXPECT_EQ(deps[0], "input/a");
    EXPECT_EQ(deps[1], "input/b");
}

TEST(DependencyGraphTest, GetTaskDependenciesForNonExistent) {
    DependencyGraph graph;
    auto deps = graph.get_task_dependencies(999);
    EXPECT_TRUE(deps.empty());
}

TEST(DependencyGraphTest, AddTaskWithRequirementsAndNoDeps) {
    DependencyGraph graph;
    graph.add_task(1, {}, caps({"gpu", "cuda"}));
    EXPECT_EQ(graph.get_ready_tasks().size(), 1);
    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_EQ(reqs.capabilities_.size(), 2u);
}

TEST(DependencyGraphTest, MarkDataReadyForPathNoTaskDependsOn) {
    DependencyGraph graph;
    graph.mark_data_ready("orphan/path");
    EXPECT_TRUE(graph.is_data_ready("orphan/path"));
    EXPECT_EQ(graph.get_ready_tasks().size(), 0);
}

// ===== TaskRequirements (timeout) 新增测试 =====

TEST(DependencyGraphTest, TaskRequirementsWithTimeout) {
    DependencyGraph graph;
    TaskRequirements spec;
    spec.capabilities_ = {"gpu", "cuda"};
    spec.timeout_seconds_ = 5.0f;
    graph.add_task(1, {}, spec);

    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_EQ(reqs.capabilities_.size(), 2u);
    EXPECT_EQ(reqs.capabilities_[0], "gpu");
    EXPECT_EQ(reqs.capabilities_[1], "cuda");
    EXPECT_FLOAT_EQ(reqs.timeout_seconds_, 5.0f);
}

TEST(DependencyGraphTest, TaskRequirementsDefaultTimeoutNegative) {
    DependencyGraph graph;
    // 默认 timeout < 0（死等）
    graph.add_task(1, {}, caps({"gpu"}));
    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_LT(reqs.timeout_seconds_, 0.0f);
    EXPECT_EQ(reqs.capabilities_.size(), 1u);
}

TEST(DependencyGraphTest, TaskRequirementsZeroTimeout) {
    DependencyGraph graph;
    TaskRequirements spec;
    spec.capabilities_ = {"gpu"};
    spec.timeout_seconds_ = 0.0f;
    graph.add_task(1, {}, spec);

    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_FLOAT_EQ(reqs.timeout_seconds_, 0.0f);
}

TEST(DependencyGraphTest, ReadyTimestampRecordedOnAdd) {
    DependencyGraph graph;
    graph.add_task(1, {});
    // 无依赖的 task 添加后立即 ready，应有时间戳
    auto ts = graph.get_task_ready_timestamp(1);
    EXPECT_TRUE(ts.has_value());
}

TEST(DependencyGraphTest, ReadyTimestampRecordedOnDepSatisfied) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});
    // 有依赖未满足：pending，无时间戳
    EXPECT_FALSE(graph.get_task_ready_timestamp(1).has_value());

    graph.mark_data_ready("input/a");
    // 依赖满足后转 ready：有时间戳
    auto ts = graph.get_task_ready_timestamp(1);
    EXPECT_TRUE(ts.has_value());
}

TEST(DependencyGraphTest, ReadyTimestampClearedOnRemove) {
    DependencyGraph graph;
    graph.add_task(1, {});
    EXPECT_TRUE(graph.get_task_ready_timestamp(1).has_value());

    graph.remove_task(1);
    EXPECT_FALSE(graph.get_task_ready_timestamp(1).has_value());
}

TEST(DependencyGraphTest, ReadyTimestampClearedOnMoveBackToPending) {
    DependencyGraph graph;
    graph.add_task(1, {"input/a"});

    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.get_task_ready_timestamp(1).has_value());

    // 数据被移除，task 从 ready 退回 pending
    graph.mark_data_removed("input/a");
    EXPECT_FALSE(graph.get_task_ready_timestamp(1).has_value());

    // 重新 ready 时记录新时间戳
    graph.mark_data_ready("input/a");
    EXPECT_TRUE(graph.get_task_ready_timestamp(1).has_value());
}

TEST(DependencyGraphTest, TaskRequirementsClearedOnRemove) {
    DependencyGraph graph;
    TaskRequirements spec;
    spec.capabilities_ = {"gpu"};
    spec.timeout_seconds_ = 3.0f;
    graph.add_task(1, {}, spec);
    graph.remove_task(1);

    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_TRUE(reqs.capabilities_.empty());  // remove 后返回空对象
}

TEST(DependencyGraphTest, ReadyTimestampNonExistentTask) {
    DependencyGraph graph;
    EXPECT_FALSE(graph.get_task_ready_timestamp(999).has_value());
}

// ===== TaskRequirements (priority) 新增测试 =====

TEST(DependencyGraphTest, TaskRequirementsPriorityField) {
    DependencyGraph graph;
    TaskRequirements spec;
    spec.capabilities_ = {"gpu"};
    spec.priority_ = 25;
    graph.add_task(1, {}, spec);

    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_EQ(reqs.priority_, 25);
    EXPECT_EQ(reqs.capabilities_.size(), 1u);
}

TEST(DependencyGraphTest, TaskRequirementsDefaultPriorityTen) {
    DependencyGraph graph;
    // 默认 priority = 10（中点值，可双向调节）
    graph.add_task(1, {}, caps({"gpu"}));
    const auto& reqs = graph.get_task_requirements(1);
    EXPECT_EQ(reqs.priority_, 10);
}

TEST(DependencyGraphTest, TaskRequirementsPriorityClearedOnRemove) {
    DependencyGraph graph;
    TaskRequirements spec;
    spec.capabilities_ = {"gpu"};
    spec.priority_ = 15;
    graph.add_task(1, {}, spec);
    graph.remove_task(1);

    const auto& reqs = graph.get_task_requirements(1);
    // remove 后返回静态空对象，priority 应为默认值 10
    EXPECT_EQ(reqs.priority_, 10);
    EXPECT_TRUE(reqs.capabilities_.empty());
}

// ── 并发正确性（P3-17 批 1）：多线程 add/mark/remove/get 全操作.mix——
//    join 后守恒断言（每 task 恰处于 ready/pending/removed 之一且计数守恒），
//    并锁 ready 有序不变量（S8-b：{-priority, task_id} 升序 ⇒ 升序遍历）。──

TEST(DependencyGraphTest, ConcurrentMarkReadyRemoveConservesTasks) {
    constexpr int kTasks = 400;
    DependencyGraph graph;
    for (int i = 0; i < kTasks; ++i) {
        graph.add_task(static_cast<uint64_t>(i), {"input/d0"});
    }
    std::latch go{4};
    std::thread t_ready([&] { go.count_down(); go.wait();
        graph.mark_data_ready("input/d0"); });
    std::thread t_rm1([&] { go.count_down(); go.wait();
        for (int i = 0; i < kTasks / 2; ++i) {
            graph.remove_task(static_cast<uint64_t>(i));
        } });
    std::thread t_rm2([&] { go.count_down(); go.wait();
        for (int i = kTasks / 2; i < kTasks; ++i) {
            if (i % 3 == 0) graph.remove_task(static_cast<uint64_t>(i));
        } });
    std::thread t_read([&] { go.count_down(); go.wait();
        for (int i = 0; i < 50; ++i) {
            (void)graph.get_ready_tasks();
            (void)graph.get_pending_tasks();
            (void)graph.is_data_ready("input/d0");
        } });
    t_ready.join(); t_rm1.join(); t_rm2.join(); t_read.join();

    // 守恒：ready ∪ pending ∪ removed == 全集（removed 计入 completed_count）。
    auto ready = graph.get_ready_tasks();
    auto pending = graph.get_pending_tasks();
    CMUnorderedSet<uint64_t> seen(ready.begin(), ready.end());
    for (auto id : pending) seen.insert(id);
    EXPECT_EQ(static_cast<int>(seen.size() + graph.completed_count()), kTasks);
}

TEST(DependencyGraphTest, ConcurrentMarkDataMixKeepsReadyOrderInvariant) {
    constexpr int kTasks = 200;
    DependencyGraph graph;
    for (int i = 0; i < kTasks; ++i) {
        graph.add_task(static_cast<uint64_t>(1000 + i), {"input/x", "input/y"});
    }
    std::latch go{3};
    std::thread t1([&] { go.count_down(); go.wait();
        graph.mark_data_ready("input/x"); });
    std::thread t2([&] { go.count_down(); go.wait();
        graph.mark_data_ready("input/y"); });
    std::thread t3([&] { go.count_down(); go.wait();
        for (int i = 0; i < 30; ++i) (void)graph.get_ready_tasks(); });
    t1.join(); t2.join(); t3.join();
    // 两依赖齐备后全部 ready，且 ready 有序不变量（{-priority, id} 升序）
    // 在并发 mark 下不被破坏。
    auto ready = graph.get_ready_tasks();
    ASSERT_EQ(ready.size(), static_cast<size_t>(kTasks));
    for (size_t i = 1; i < ready.size(); ++i) {
        EXPECT_LT(ready[i - 1], ready[i]) << "ready 集顺序破坏 @" << i;
    }
}

TEST(DependencyGraphTest, ConcurrentDataReadyRemovedFlipEndsDeterministic) {
    DependencyGraph graph;
    graph.add_task(1, {"input/z"});
    std::latch go{2};
    std::thread t_ready([&] { go.count_down(); go.wait();
        graph.mark_data_ready("input/z"); });
    std::thread t_removed([&] { go.count_down(); go.wait();
        graph.mark_data_removed("input/z"); });
    t_ready.join(); t_removed.join();
    // 后写者胜出（锁内串行化），get_ready_tasks 与最终数据状态一致。
    bool data_ready = graph.is_data_ready("input/z");
    auto ready = graph.get_ready_tasks();
    EXPECT_EQ(ready.empty(), !data_ready);
}

}  // namespace fly
