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
    
    auto manager_obs = as_shared(manager);  // 观察句柄存活至测试末尾（同
                                            // task_scheduler_test 注释）
    HeartbeatMonitor monitor(manager_obs, 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 0);
}

TEST(HeartbeatMonitorTest, DetectDeadWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    auto manager_obs = as_shared(manager);  // 观察句柄存活至测试末尾（同
                                            // task_scheduler_test 注释）
    HeartbeatMonitor monitor(manager_obs, 30);
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
    
    auto manager_obs = as_shared(manager);  // 观察句柄存活至测试末尾（同
                                            // task_scheduler_test 注释）
    HeartbeatMonitor monitor(manager_obs, 30);
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
    
    auto manager_obs = as_shared(manager);  // 观察句柄存活至测试末尾（同
                                            // task_scheduler_test 注释）
    HeartbeatMonitor monitor(manager_obs, 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
    EXPECT_EQ(dead[0], 2);
}

TEST(HeartbeatMonitorTest, TimeoutConfiguration) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    auto manager_obs = as_shared(manager);  // 观察句柄存活至测试末尾（同
                                            // task_scheduler_test 注释）
    HeartbeatMonitor monitor(manager_obs, 30);
    EXPECT_EQ(monitor.get_timeout(), 30);
    
    monitor.set_timeout(60);
    EXPECT_EQ(monitor.get_timeout(), 60);
}

TEST(HeartbeatMonitorTest, CustomTimeout) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    auto manager_obs = as_shared(manager);  // 观察句柄存活至测试末尾
    HeartbeatMonitor monitor(manager_obs, 10);
    monitor.check_all_workers(15);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
}

TEST(HeartbeatMonitorTest, AlreadyDeadNotReprocessed) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.update_worker_status(1, WorkerStatus::DEAD);
    
    auto manager_obs = as_shared(manager);  // 观察句柄存活至测试末尾（同
                                            // task_scheduler_test 注释）
    HeartbeatMonitor monitor(manager_obs, 30);
    monitor.check_all_workers(100);
    
    auto dead = monitor.get_dead_workers();
    EXPECT_EQ(dead.size(), 1);
}

// 弱观察失效（§16 判据）：宿主释放 manager 后检查必须安全跳过（不悬垂）。
TEST(HeartbeatMonitorTest, ExpiredManagerSkipsCheck) {
    auto manager = CMMakeShared<WorkerManager>();
    HeartbeatMonitor monitor(manager, 30);
    manager.reset();
    monitor.check_all_workers(1000);
    EXPECT_TRUE(monitor.get_dead_workers().empty());
}

}  // namespace fly