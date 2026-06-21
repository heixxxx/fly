#include <gtest/gtest.h>
#include <task/cpp/task_scheduler.h>
#include <chrono>
#include <thread>

namespace fly {

// 构造仅含 capabilities 的 TaskRequirements（timeout 默认 <0 即死等，等价旧语义）
static TaskRequirements caps(CMVector<CMString> c) {
    TaskRequirements r;
    r.capabilities_ = std::move(c);
    return r;
}

static TaskRequirements caps_timeout(CMVector<CMString> c, float timeout) {
    TaskRequirements r;
    r.capabilities_ = std::move(c);
    r.timeout_seconds_ = timeout;
    return r;
}

TEST(TaskSchedulerTest, ScheduleNoReadyTasks) {
    DependencyGraph graph;
    WorkerManager manager;
    TaskScheduler scheduler(&graph, &manager);

    auto result = scheduler.schedule_next();
    EXPECT_FALSE(result.scheduled_);
}

TEST(TaskSchedulerTest, ScheduleNoIdleWorkers) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.assign_task(1, 100);

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    EXPECT_FALSE(result.scheduled_);
}

TEST(TaskSchedulerTest, ScheduleSingleTask) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_EQ(result.worker_id_, 1);
    EXPECT_FALSE(result.degraded_);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::BUSY);
}

TEST(TaskSchedulerTest, ScheduleMultipleTasksFIFO) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});
    graph.add_task(2, {});
    graph.add_task(3, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});

    TaskScheduler scheduler(&graph, &manager);
    auto results = scheduler.schedule_all_available();

    EXPECT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].task_id_, 1);
    EXPECT_EQ(results[1].task_id_, 2);
}

TEST(TaskSchedulerTest, ScheduleWithDependencies) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});
    graph.add_task(2, {"output/1"});
    manager.register_worker(1, "127.0.0.1", 8080, {});

    TaskScheduler scheduler(&graph, &manager);

    auto result1 = scheduler.schedule_next();
    EXPECT_TRUE(result1.scheduled_);
    EXPECT_EQ(result1.task_id_, 1);

    auto result2 = scheduler.schedule_next();
    EXPECT_FALSE(result2.scheduled_);

    manager.complete_task(1);
    graph.mark_data_ready("output/1");
    auto result3 = scheduler.schedule_next();
    EXPECT_TRUE(result3.scheduled_);
    EXPECT_EQ(result3.task_id_, 2);
}

TEST(TaskSchedulerTest, LocalityPreferenceToggle) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});

    TaskScheduler scheduler(&graph, &manager);
    scheduler.set_locality_preference(false);

    auto result = scheduler.schedule_next();
    EXPECT_TRUE(result.scheduled_);
}

TEST(TaskSchedulerTest, CapabilityMatch) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps({"gpu"}));
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu", "cuda"});
    manager.register_worker(2, "127.0.0.1", 8081, {});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_EQ(result.worker_id_, 1);
    EXPECT_FALSE(result.degraded_);
}

TEST(TaskSchedulerTest, NoMatchingWorker) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps({"gpu"}));
    manager.register_worker(1, "127.0.0.1", 8080, {});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_FALSE(result.scheduled_);  // 死等，无完整匹配不调度
}

TEST(TaskSchedulerTest, PartialCapabilityMismatch) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps({"gpu", "large_memory"}));
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"large_memory"});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_FALSE(result.scheduled_);  // 死等，无完整匹配不调度
}

TEST(TaskSchedulerTest, MixedCapabilitiesAndConstraints) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps({"gpu"}));
    graph.add_task(2, {}, {});
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {});

    TaskScheduler scheduler(&graph, &manager);
    auto results = scheduler.schedule_all_available();

    EXPECT_EQ(results.size(), 2u);

    EXPECT_EQ(results[0].task_id_, 1);
    EXPECT_EQ(results[0].worker_id_, 1);

    EXPECT_EQ(results[1].task_id_, 2);
}

TEST(TaskSchedulerTest, ConstrainedTaskDoesNotBlockUnconstrained) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps({"gpu"}));
    graph.add_task(2, {}, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});

    TaskScheduler scheduler(&graph, &manager);
    auto results = scheduler.schedule_all_available();

    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].task_id_, 2);
}

TEST(TaskSchedulerTest, MultipleWorkersWithSameCapability) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps({"gpu"}));
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"gpu"});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_TRUE(result.worker_id_ == 1 || result.worker_id_ == 2);
}

TEST(TaskSchedulerTest, ScheduleAllAvailableExhaustsReadyAndIdle) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});
    graph.add_task(2, {});
    graph.add_task(3, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});

    TaskScheduler scheduler(&graph, &manager);
    auto results = scheduler.schedule_all_available();

    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].task_id_, 1);
}

TEST(TaskSchedulerTest, ScheduleAllAvailableEmptyGraph) {
    DependencyGraph graph;
    WorkerManager manager;

    TaskScheduler scheduler(&graph, &manager);
    auto results = scheduler.schedule_all_available();
    EXPECT_TRUE(results.empty());
}

TEST(TaskSchedulerTest, ScheduleRemovesTaskFromReady) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});
    graph.add_task(2, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});

    TaskScheduler scheduler(&graph, &manager);
    auto r1 = scheduler.schedule_next();
    EXPECT_TRUE(r1.scheduled_);
    EXPECT_EQ(r1.task_id_, 1);

    auto r2 = scheduler.schedule_next();
    EXPECT_FALSE(r2.scheduled_);
}

TEST(TaskSchedulerTest, ScheduleNextWithMultipleCapabilities) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps({"gpu", "cuda"}));
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu", "cuda", "python"});
    manager.register_worker(2, "127.0.0.1", 8081, {"gpu"});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_EQ(result.worker_id_, 1);
}

// ===== Attribute Timeout 新增测试 =====

// timeout > 0 未到期时不降级，死等完整匹配
TEST(TaskSchedulerTest, ScheduleWithAttrTimeoutNotExpiredStillWaits) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps_timeout({"gpu"}, 10.0f));  // 10秒超时
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});  // 无 gpu

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    // 未到期，不降级
    EXPECT_FALSE(result.scheduled_);
}

// timeout > 0 到期后降级到匹配属性最多的 worker
TEST(TaskSchedulerTest, ScheduleWithAttrTimeoutExpiredDegradesToMostMatches) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps_timeout({"gpu", "cuda"}, 0.05f));  // 50ms 超时
    // worker1: 匹配 0 个；worker2: 匹配 1 个（cuda）；worker3: 匹配 2 个但非完全匹配（不可能，这里构造匹配数差异）
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"cuda"});
    manager.register_worker(3, "127.0.0.1", 8082, {"cuda", "memory"});

    TaskScheduler scheduler(&graph, &manager);

    // 第一次调度：未到期，死等
    auto result1 = scheduler.schedule_next();
    EXPECT_FALSE(result1.scheduled_);

    // 等待超时
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 第二次调度：已超时，降级到匹配最多的 worker（worker2 和 worker3 都匹配1个：cuda）
    auto result2 = scheduler.schedule_next();
    EXPECT_TRUE(result2.scheduled_);
    EXPECT_EQ(result2.task_id_, 1);
    EXPECT_TRUE(result2.degraded_);
    // 应该选匹配属性最多的 worker（worker2 和 worker3 都匹配1个 cuda）
    EXPECT_TRUE(result2.worker_id_ == 2 || result2.worker_id_ == 3);
}

// timeout == 0 立即降级
TEST(TaskSchedulerTest, ScheduleWithAttrTimeoutZeroImmediateDegrade) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps_timeout({"gpu"}, 0.0f));  // 立即降级
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});  // 无 gpu

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_EQ(result.worker_id_, 1);
    EXPECT_TRUE(result.degraded_);
}

// timeout < 0 死等，不降级
TEST(TaskSchedulerTest, ScheduleWithAttrTimeoutNegativeWaitForever) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps_timeout({"gpu"}, -1.0f));  // 死等
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});  // 无 gpu

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_FALSE(result.scheduled_);  // 死等，不降级
}

// 即使允许降级，有完整匹配时优先选完整匹配（且 degraded_=false）
TEST(TaskSchedulerTest, ScheduleWithAttrTimeoutPreferFullMatch) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps_timeout({"gpu"}, 0.0f));  // 允许降级
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});   // 无匹配
    manager.register_worker(2, "127.0.0.1", 8081, {"gpu"});   // 完整匹配

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.worker_id_, 2);  // 完整匹配优先
    EXPECT_FALSE(result.degraded_);
}

// waiting 的 task 不阻塞其他无约束 task 的调度
TEST(TaskSchedulerTest, WaitingTaskDoesNotBlockOthers) {
    DependencyGraph graph;
    WorkerManager manager;

    // task1: 死等 gpu，但集群中无 gpu worker
    graph.add_task(1, {}, caps({"gpu"}));
    // task2: 无属性要求
    graph.add_task(2, {});
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});

    TaskScheduler scheduler(&graph, &manager);
    auto results = scheduler.schedule_all_available();

    // task1 waiting 被跳过，task2 正常调度
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].task_id_, 2);
    EXPECT_FALSE(results[0].degraded_);
}

// timeout > 0 到期后，多 worker 部分匹配时选匹配最多的
TEST(TaskSchedulerTest, DegradedScheduleSelectsMostMatchedWorker) {
    DependencyGraph graph;
    WorkerManager manager;

    // 需要 3 个属性
    graph.add_task(1, {}, caps_timeout({"a", "b", "c"}, 0.05f));
    manager.register_worker(1, "127.0.0.1", 8080, {"a"});            // 匹配 1
    manager.register_worker(2, "127.0.0.1", 8081, {"a", "b"});       // 匹配 2（最多）
    manager.register_worker(3, "127.0.0.1", 8082, {"a"});            // 匹配 1

    TaskScheduler scheduler(&graph, &manager);

    // 第一次：未到期，死等
    EXPECT_FALSE(scheduler.schedule_next().scheduled_);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 第二次：已超时，降级到匹配最多的 worker2
    auto result = scheduler.schedule_next();
    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.worker_id_, 2);  // 匹配 2 个的 worker
    EXPECT_TRUE(result.degraded_);
}

// ===== 数据依赖 + 属性 timeout 组合场景（两阶段调度核心） =====

// timeout 计时从数据依赖满足后开始，而非提交时。
// task 有数据依赖且属性 timeout=短时间，提交后等待一段时间（此时数据依赖未满足，
// timeout 不应开始计时），再满足数据依赖，此时 timeout 从头开始计时。
TEST(TaskSchedulerTest, AttrTimeoutStartsAfterDataDepSatisfied) {
    DependencyGraph graph;
    WorkerManager manager;

    // task 要求 gpu，timeout=0.05s（50ms），但依赖 output/a
    graph.add_task(1, {"output/a"}, caps_timeout({"gpu"}, 0.05f));
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});  // 无 gpu

    TaskScheduler scheduler(&graph, &manager);

    // 提交后等 100ms（远超 timeout），但数据依赖未满足 → timeout 不应开始
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(scheduler.schedule_next().scheduled_);  // pending，未调度

    // 满足数据依赖，task 进入 ready，timeout 从此刻开始
    graph.mark_data_ready("output/a");

    // 立即调度：timeout 刚开始，未到期 → 死等
    EXPECT_FALSE(scheduler.schedule_next().scheduled_);

    // 再等 100ms，此时 timeout 到期 → 降级
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto result = scheduler.schedule_next();
    EXPECT_TRUE(result.scheduled_);
    EXPECT_TRUE(result.degraded_);
    EXPECT_EQ(result.worker_id_, 1);
}

// 数据依赖满足后立即 timeout=0 降级
TEST(TaskSchedulerTest, AttrTimeoutZeroAfterDataDepSatisfied) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {"output/a"}, caps_timeout({"gpu"}, 0.0f));
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});

    TaskScheduler scheduler(&graph, &manager);

    // 数据依赖未满足：不调度
    EXPECT_FALSE(scheduler.schedule_next().scheduled_);

    // 满足数据依赖：timeout=0 立即降级
    graph.mark_data_ready("output/a");
    auto result = scheduler.schedule_next();
    EXPECT_TRUE(result.scheduled_);
    EXPECT_TRUE(result.degraded_);
    EXPECT_EQ(result.worker_id_, 1);
}

// 数据依赖满足后死等(timeout<0)：属性不满足永不调度
TEST(TaskSchedulerTest, AttrTimeoutNegativeAfterDataDepSatisfied) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {"output/a"}, caps_timeout({"gpu"}, -1.0f));
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});

    TaskScheduler scheduler(&graph, &manager);

    graph.mark_data_ready("output/a");
    // 死等：即使数据依赖满足，属性不匹配也不调度
    EXPECT_FALSE(scheduler.schedule_next().scheduled_);
}

// 数据依赖链：task1 无要求，task2 依赖 task1 输出且要求 gpu(timeout)。
// task1 完成后 task2 进入 ready，此时 timeout 开始计时。
TEST(TaskSchedulerTest, AttrTimeoutInDependencyChain) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {});  // task1 无依赖无要求
    // task2 依赖 task1 的输出，要求 gpu，timeout=0.05s
    graph.add_task(2, {"output/1"}, caps_timeout({"gpu"}, 0.05f));
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});  // 无 gpu

    TaskScheduler scheduler(&graph, &manager);

    // task1 可调度
    auto r1 = scheduler.schedule_next();
    EXPECT_TRUE(r1.scheduled_);
    EXPECT_EQ(r1.task_id_, 1);

    // task2 数据依赖未满足
    EXPECT_FALSE(scheduler.schedule_next().scheduled_);

    // task1 完成，产出 output/1，task2 数据依赖满足，timeout 开始
    manager.complete_task(1);
    graph.mark_data_ready("output/1");

    // task2 立即调度：timeout 未到期，死等
    EXPECT_FALSE(scheduler.schedule_next().scheduled_);

    // 等 timeout 到期
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto r2 = scheduler.schedule_next();
    EXPECT_TRUE(r2.scheduled_);
    EXPECT_EQ(r2.task_id_, 2);
    EXPECT_TRUE(r2.degraded_);
}

// 数据依赖满足时属性刚好可用：无需等待 timeout，立即调度（非降级）
TEST(TaskSchedulerTest, AttrTimeoutFullMatchWhenDataDepSatisfied) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {"output/a"}, caps_timeout({"gpu"}, 5.0f));
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu"});  // 有 gpu

    TaskScheduler scheduler(&graph, &manager);

    graph.mark_data_ready("output/a");
    auto result = scheduler.schedule_next();
    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.worker_id_, 1);
    EXPECT_FALSE(result.degraded_);  // 完整匹配，非降级
}

// ===== worker 匹配数相同时降级行为 =====

// 多个 worker 匹配数相同（都为 0）：降级到第一个 idle worker（按 ID 排序）
TEST(TaskSchedulerTest, DegradeAllWorkersSameMatchCountSelectsFirst) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps_timeout({"gpu"}, 0.0f));  // 立即降级
    // 三个 worker 都无 gpu，匹配数都为 0
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"cpu"});
    manager.register_worker(3, "127.0.0.1", 8082, {"cpu"});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_TRUE(result.degraded_);
    // 匹配数相同时，选第一个 idle worker（get_idle_workers 按 ID 排序）
    EXPECT_EQ(result.worker_id_, 1);
}

// 多个 worker 部分匹配数相同（都为 1）：降级到第一个达到最佳匹配数的 worker
TEST(TaskSchedulerTest, DegradeWorkersWithSamePartialMatchSelectsFirst) {
    DependencyGraph graph;
    WorkerManager manager;

    // 要求 3 个属性，但所有 worker 都只匹配 1 个
    graph.add_task(1, {}, caps_timeout({"a", "b", "c"}, 0.0f));
    manager.register_worker(1, "127.0.0.1", 8080, {"a"});  // 匹配 1
    manager.register_worker(2, "127.0.0.1", 8081, {"b"});  // 匹配 1
    manager.register_worker(3, "127.0.0.1", 8082, {"c"});  // 匹配 1

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_TRUE(result.degraded_);
    // 匹配数都为 1，选第一个达到最佳匹配的 worker（worker 1）
    EXPECT_EQ(result.worker_id_, 1);
}

// 降级调度消耗 idle worker 后，后续无约束 task 仍可调度到剩余 worker
TEST(TaskSchedulerTest, DegradeScheduleThenUnconstrainedTaskSchedules) {
    DependencyGraph graph;
    WorkerManager manager;

    graph.add_task(1, {}, caps_timeout({"gpu"}, 0.0f));  // 降级到 worker1
    graph.add_task(2, {});                                // 无约束，调度到 worker2
    manager.register_worker(1, "127.0.0.1", 8080, {"cpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"cpu"});

    TaskScheduler scheduler(&graph, &manager);
    auto results = scheduler.schedule_all_available();

    EXPECT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].task_id_, 1);
    EXPECT_TRUE(results[0].degraded_);
    EXPECT_EQ(results[1].task_id_, 2);
    EXPECT_FALSE(results[1].degraded_);
}

}  // namespace fly
