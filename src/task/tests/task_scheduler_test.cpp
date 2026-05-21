#include <gtest/gtest.h>
#include <task/cpp/task_scheduler.h>

namespace fly {

TEST(TaskSchedulerTest, ScheduleNoReadyTasks) {
    DependencyGraph graph;
    WorkerManager manager;
    TaskScheduler scheduler(&graph, &manager);
    
    auto result = scheduler.schedule_next();
    EXPECT_FALSE(result.scheduled);
}

TEST(TaskSchedulerTest, ScheduleNoIdleWorkers) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.assign_task(1, 100);
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    EXPECT_FALSE(result.scheduled);
}

TEST(TaskSchedulerTest, ScheduleSingleTask) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_TRUE(result.scheduled);
    EXPECT_EQ(result.task_id, 1);
    EXPECT_EQ(result.worker_id, 1);
    EXPECT_EQ(manager.get_worker(1)->status, WorkerStatus::BUSY);
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
    EXPECT_EQ(results[0].task_id, 1);
    EXPECT_EQ(results[1].task_id, 2);
}

TEST(TaskSchedulerTest, ScheduleWithDependencies) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {});
    graph.add_task(2, {"output/1"});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    TaskScheduler scheduler(&graph, &manager);
    
    auto result1 = scheduler.schedule_next();
    EXPECT_TRUE(result1.scheduled);
    EXPECT_EQ(result1.task_id, 1);
    
    auto result2 = scheduler.schedule_next();
    EXPECT_FALSE(result2.scheduled);
    
    manager.complete_task(1);
    graph.mark_data_ready("output/1");
    auto result3 = scheduler.schedule_next();
    EXPECT_TRUE(result3.scheduled);
    EXPECT_EQ(result3.task_id, 2);
}

TEST(TaskSchedulerTest, LocalityPreferenceToggle) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    TaskScheduler scheduler(&graph, &manager);
    scheduler.set_locality_preference(false);
    
    auto result = scheduler.schedule_next();
    EXPECT_TRUE(result.scheduled);
}

TEST(TaskSchedulerTest, CapabilityMatch) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {}, {"gpu"});
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu", "cuda"});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_TRUE(result.scheduled);
    EXPECT_EQ(result.task_id, 1);
    EXPECT_EQ(result.worker_id, 1);
}

TEST(TaskSchedulerTest, NoMatchingWorker) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {}, {"gpu"});
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_FALSE(result.scheduled);
}

TEST(TaskSchedulerTest, PartialCapabilityMismatch) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {}, {"gpu", "large_memory"});
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"large_memory"});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_FALSE(result.scheduled);
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
    
    EXPECT_EQ(results[0].task_id, 1);
    EXPECT_EQ(results[0].worker_id, 1);
    
    EXPECT_EQ(results[1].task_id, 2);
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
    EXPECT_EQ(results[0].task_id, 2);
}

TEST(TaskSchedulerTest, MultipleWorkersWithSameCapability) {
    DependencyGraph graph;
    WorkerManager manager;
    
    graph.add_task(1, {}, {"gpu"});
    manager.register_worker(1, "127.0.0.1", 8080, {"gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"gpu"});
    
    TaskScheduler scheduler(&graph, &manager);
    auto result = scheduler.schedule_next();
    
    EXPECT_TRUE(result.scheduled);
    EXPECT_EQ(result.task_id, 1);
    EXPECT_TRUE(result.worker_id == 1 || result.worker_id == 2);
}

}  // namespace fly