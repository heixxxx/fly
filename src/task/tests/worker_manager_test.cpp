#include <gtest/gtest.h>
#include <task/cpp/worker_manager.h>

namespace fly {

TEST(WorkerManagerTest, RegisterWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python", "gpu"});
    
    EXPECT_EQ(manager.get_worker_count(), 1);
    auto* worker = manager.get_worker(1);
    ASSERT_NE(worker, nullptr);
    EXPECT_EQ(worker->address, "127.0.0.1");
    EXPECT_EQ(worker->port, 8080);
    EXPECT_EQ(worker->status, WorkerStatus::IDLE);
    EXPECT_EQ(worker->capabilities.size(), 2);
}

TEST(WorkerManagerTest, UnregisterWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.unregister_worker(1);
    EXPECT_EQ(manager.get_worker_count(), 0);
    EXPECT_EQ(manager.get_worker(1), nullptr);
}

TEST(WorkerManagerTest, UpdateWorkerStatus) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    manager.update_worker_status(1, WorkerStatus::BUSY);
    EXPECT_EQ(manager.get_worker(1)->status, WorkerStatus::BUSY);
    
    manager.update_worker_status(1, WorkerStatus::DEAD);
    EXPECT_EQ(manager.get_worker(1)->status, WorkerStatus::DEAD);
}

TEST(WorkerManagerTest, RecordHeartbeat) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    auto old_time = manager.get_worker(1)->last_heartbeat;
    manager.record_heartbeat(1);
    auto new_time = manager.get_worker(1)->last_heartbeat;
    EXPECT_GE(new_time, old_time);
}

TEST(WorkerManagerTest, AssignAndCompleteTask) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    manager.assign_task(1, 100);
    EXPECT_EQ(manager.get_worker(1)->status, WorkerStatus::BUSY);
    EXPECT_EQ(manager.get_worker(1)->current_task_id, 100);
    
    manager.complete_task(1);
    EXPECT_EQ(manager.get_worker(1)->status, WorkerStatus::IDLE);
    EXPECT_EQ(manager.get_worker(1)->current_task_id, 0);
}

TEST(WorkerManagerTest, GetIdleWorkers) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    manager.assign_task(1, 100);
    
    auto idle = manager.get_idle_workers();
    EXPECT_EQ(idle.size(), 1);
    EXPECT_EQ(idle[0], 2);
    EXPECT_EQ(manager.get_idle_worker_count(), 1);
}

TEST(WorkerManagerTest, GetWorkersWithCapability) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python", "gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"python"});
    manager.register_worker(3, "127.0.0.1", 8082, {"cpp"});
    
    auto gpu_workers = manager.get_workers_with_capability("gpu");
    EXPECT_EQ(gpu_workers.size(), 1);
    EXPECT_EQ(gpu_workers[0], 1);
    
    auto python_workers = manager.get_workers_with_capability("python");
    EXPECT_EQ(python_workers.size(), 2);
}

TEST(WorkerManagerTest, GetAllWorkers) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    
    auto all = manager.get_all_workers();
    EXPECT_EQ(all.size(), 2);
}

}  // namespace fly