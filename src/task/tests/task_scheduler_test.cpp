#include <gtest/gtest.h>
#include <task/cpp/task_scheduler.h>

namespace fly {

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
    
    graph.add_task(1, {}, {"gpu"});
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu", "cuda"});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_EQ(result.worker_id_, 1);
}

TEST(TaskSchedulerTest, NoMatchingWorker) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {}, {"gpu"});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_FALSE(result.scheduled_);
}

TEST(TaskSchedulerTest, PartialCapabilityMismatch) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {}, {"gpu", "large_memory"});
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"large_memory"});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_FALSE(result.scheduled_);
}

TEST(TaskSchedulerTest, MixedCapabilitiesAndConstraints) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {}, {"gpu"});
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
    
    graph.add_task(1, {}, {"gpu"});
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
    
    graph.add_task(1, {}, {"gpu"});
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

    graph.add_task(1, {}, {"gpu", "cuda"});
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu", "cuda", "python"});
    manager.register_worker(2, "127.0.0.1", 8081, {"gpu"});

    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();

    EXPECT_TRUE(result.scheduled_);
    EXPECT_EQ(result.task_id_, 1);
    EXPECT_EQ(result.worker_id_, 1);
}

}  // namespace fly
