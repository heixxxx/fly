#include <gtest/gtest.h>
#include <task/cpp/heartbeat_monitor.h>

namespace fly {

TEST(HeartbeatMonitorTest, NoDeadWorkers) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.set_heartbeat(1, 80);
    
    HeartbeatMonitor monitor(&manager, 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 0);
}

TEST(HeartbeatMonitorTest, DetectDeadWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    HeartbeatMonitor monitor(&manager, 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
    EXPECT_EQ(dead[0], 1);
    EXPECT_EQ(manager.get_worker(1)->get().status, WorkerStatus::DEAD);
}

TEST(HeartbeatMonitorTest, AliveWorkerNotMarkedDead) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.set_heartbeat(1, 30);
    
    HeartbeatMonitor monitor(&manager, 30);
    monitor.check_all_workers(50);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 0);
    EXPECT_EQ(manager.get_worker(1)->get().status, WorkerStatus::IDLE);
}

TEST(HeartbeatMonitorTest, MultipleWorkersMixedStatus) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    manager.register_worker(3, "127.0.0.1", 8082, {});
    
    manager.set_heartbeat(1, 80);
    manager.set_heartbeat(3, 80);
    
    HeartbeatMonitor monitor(&manager, 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
    EXPECT_EQ(dead[0], 2);
}

TEST(HeartbeatMonitorTest, TimeoutConfiguration) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    HeartbeatMonitor monitor(&manager, 30);
    EXPECT_EQ(monitor.get_timeout(), 30);
    
    monitor.set_timeout(60);
    EXPECT_EQ(monitor.get_timeout(), 60);
}

TEST(HeartbeatMonitorTest, CustomTimeout) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    HeartbeatMonitor monitor(&manager, 10);
    monitor.check_all_workers(15);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
}

TEST(HeartbeatMonitorTest, AlreadyDeadNotReprocessed) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.update_worker_status(1, WorkerStatus::DEAD);
    
    HeartbeatMonitor monitor(&manager, 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
}

}  // namespace fly