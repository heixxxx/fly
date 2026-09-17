#include <gtest/gtest.h>
#include <task/cpp/heartbeat_monitor.h>

namespace {
// 栈对象 → 非拥有观察句柄（空删除器 shared_ptr）：同 task_scheduler_test 注释。
template <typename T>
CMSharedPtr<T> as_shared(T& obj) {
    return CMSharedPtr<T>(&obj, [](T*) {});
}
}  // namespace

namespace fly {

TEST(HeartbeatMonitorTest, NoDeadWorkers) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.set_heartbeat(1, 80);
    
    HeartbeatMonitor monitor(as_shared(manager), 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 0);
}

TEST(HeartbeatMonitorTest, DetectDeadWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    HeartbeatMonitor monitor(as_shared(manager), 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
    EXPECT_EQ(dead[0], 1);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::DEAD);
}

TEST(HeartbeatMonitorTest, AliveWorkerNotMarkedDead) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.set_heartbeat(1, 30);
    
    HeartbeatMonitor monitor(as_shared(manager), 30);
    monitor.check_all_workers(50);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 0);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::IDLE);
}

TEST(HeartbeatMonitorTest, MultipleWorkersMixedStatus) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    manager.register_worker(3, "127.0.0.1", 8082, {});
    
    manager.set_heartbeat(1, 80);
    manager.set_heartbeat(3, 80);
    
    HeartbeatMonitor monitor(as_shared(manager), 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
    EXPECT_EQ(dead[0], 2);
}

TEST(HeartbeatMonitorTest, TimeoutConfiguration) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    HeartbeatMonitor monitor(as_shared(manager), 30);
    EXPECT_EQ(monitor.get_timeout(), 30);
    
    monitor.set_timeout(60);
    EXPECT_EQ(monitor.get_timeout(), 60);
}

TEST(HeartbeatMonitorTest, CustomTimeout) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    HeartbeatMonitor monitor(as_shared(manager), 10);
    monitor.check_all_workers(15);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
}

TEST(HeartbeatMonitorTest, AlreadyDeadNotReprocessed) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.update_worker_status(1, WorkerStatus::DEAD);
    
    HeartbeatMonitor monitor(as_shared(manager), 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
}

// §16 悬垂防护回归：宿主释放句柄后，观察者强持延寿——检查安全继续（不悬垂）。
TEST(HeartbeatMonitorTest, OutlivesHostResetSafely) {
    auto manager = CMMakeShared<WorkerManager>();
    manager->register_worker(1, "127.0.0.1", 8080, {});
    manager->set_heartbeat(1, 0);
    HeartbeatMonitor monitor(manager, 30);

    manager.reset();   // 模拟宿主重建
    monitor.check_all_workers(1000);
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);   // 延寿的旧对象上正常判死
    EXPECT_EQ(dead[0], 1u);
}

}  // namespace fly