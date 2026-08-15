#include <gtest/gtest.h>
#include <task/cpp/worker_manager.h>

namespace fly {

TEST(WorkerManagerTest, RegisterWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python", "gpu"});

    EXPECT_EQ(manager.get_worker_count(), 1);
    auto worker_opt = manager.get_worker(1);
    ASSERT_TRUE(worker_opt.has_value());
    auto& worker = worker_opt->get();
    EXPECT_EQ(worker.address_, "127.0.0.1");
    EXPECT_EQ(worker.port_, 8080);
    EXPECT_EQ(worker.status_, WorkerStatus::IDLE);
    EXPECT_EQ(worker.capabilities_.size(), 2);
    EXPECT_EQ(worker.role_, WorkerRole::HYBRID) << "缺省 role 应为 hybrid";
}

// role 静态身份：注册时设定存储进 WorkerInfo（默认 hybrid；storage_only 显式）。
TEST(WorkerManagerTest, RegisterWorkerStoresRole) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, CMVector<CMString>{}, "", "",
                            WorkerRole::STORAGE_ONLY);
    manager.register_worker(2, "127.0.0.1", 8081);

    EXPECT_EQ(manager.get_worker(1)->get().role_, WorkerRole::STORAGE_ONLY);
    EXPECT_EQ(manager.get_worker(2)->get().role_, WorkerRole::HYBRID);
}

// 调度决策不感知 storage_only（用户确认语义）：idle 候选层过滤——storage_only
// 注册后 idle 恒空；hybrid 照常。它仍在 get_all_workers（心跳判死/数据面）。
TEST(WorkerManagerTest, GetIdleWorkersExcludesStorageOnly) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, CMVector<CMString>{}, "", "",
                            WorkerRole::STORAGE_ONLY);
    manager.register_worker(2, "127.0.0.1", 8081);
    manager.register_worker(3, "127.0.0.1", 8082, CMVector<CMString>{}, "", "",
                            WorkerRole::STORAGE_ONLY);

    auto idle = manager.get_idle_workers();
    ASSERT_EQ(idle.size(), 1u);
    EXPECT_EQ(idle[0], 2u) << "storage_only workers must not be schedulable candidates";
    EXPECT_EQ(manager.get_idle_worker_count(), 1u);

    // storage_only 仍参与心跳判死（get_all_workers 含它）。
    EXPECT_EQ(manager.get_all_workers().size(), 3u);
}

TEST(WorkerManagerTest, UnregisterWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.unregister_worker(1);
    EXPECT_EQ(manager.get_worker_count(), 0);
    EXPECT_FALSE(manager.get_worker(1).has_value());
}

TEST(WorkerManagerTest, UpdateWorkerStatus) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    manager.update_worker_status(1, WorkerStatus::BUSY);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::BUSY);
    
    manager.update_worker_status(1, WorkerStatus::DEAD);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::DEAD);
}

TEST(WorkerManagerTest, RecordHeartbeat) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    auto old_time = manager.get_worker(1)->get().last_heartbeat_;
    manager.record_heartbeat(1);
    auto new_time = manager.get_worker(1)->get().last_heartbeat_;
    EXPECT_GE(new_time, old_time);
}

TEST(WorkerManagerTest, AssignAndCompleteTask) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    
    manager.assign_task(1, 100);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::BUSY);
    EXPECT_EQ(manager.get_worker(1)->get().current_task_id_, 100);
    
    manager.complete_task(1);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::IDLE);
    EXPECT_EQ(manager.get_worker(1)->get().current_task_id_, 0);
}

// 精确回滚：cancel_task_if_assigned 只回滚"正持有该 task 的 BUSY worker"，
// 不误动其它 task/状态（send_merge_task 未连接路径的 assign 回滚语义）。
TEST(WorkerManagerTest, CancelTaskIfAssigned) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});

    manager.assign_task(1, 100);
    manager.assign_task(2, 200);

    // task_id 不匹配：不回滚。
    manager.cancel_task_if_assigned(1, 999);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::BUSY);

    // 精确匹配：回滚 worker1。
    manager.cancel_task_if_assigned(1, 100);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::IDLE);
    EXPECT_EQ(manager.get_worker(1)->get().current_task_id_, 0);
    // 其它 worker 不受影响。
    EXPECT_EQ(manager.get_worker(2)->get().status_, WorkerStatus::BUSY);

    // IDLE worker 再 cancel：no-op（无任务可回滚）。
    manager.cancel_task_if_assigned(1, 100);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::IDLE);
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

TEST(WorkerManagerTest, UpdateCapabilitiesAddOnly) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python"});

    manager.update_capabilities(1, {"gpu", "cuda"}, {});

    auto worker_opt1 = manager.get_worker(1);
    ASSERT_TRUE(worker_opt1.has_value());
    EXPECT_EQ(worker_opt1->get().capabilities_.size(), 3);

    auto gpu_workers = manager.get_workers_with_capability("gpu");
    EXPECT_EQ(gpu_workers.size(), 1);
    EXPECT_EQ(gpu_workers[0], 1);
}

TEST(WorkerManagerTest, UpdateCapabilitiesRemoveOnly) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python", "gpu", "cuda"});

    manager.update_capabilities(1, {}, {"gpu"});

    auto worker_opt2 = manager.get_worker(1);
    ASSERT_TRUE(worker_opt2.has_value());
    EXPECT_EQ(worker_opt2->get().capabilities_.size(), 2);

    auto gpu_workers = manager.get_workers_with_capability("gpu");
    EXPECT_TRUE(gpu_workers.empty());
}

TEST(WorkerManagerTest, UpdateCapabilitiesAddAndRemoveSimultaneously) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python", "gpu"});

    manager.update_capabilities(1, {"cuda"}, {"gpu"});

    auto worker_opt3 = manager.get_worker(1);
    ASSERT_TRUE(worker_opt3.has_value());
    EXPECT_EQ(worker_opt3->get().capabilities_.size(), 2);

    auto gpu_workers = manager.get_workers_with_capability("gpu");
    EXPECT_TRUE(gpu_workers.empty());

    auto cuda_workers = manager.get_workers_with_capability("cuda");
    EXPECT_EQ(cuda_workers.size(), 1);
}

TEST(WorkerManagerTest, UpdateCapabilitiesDeduplicateOnAdd) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python"});

    manager.update_capabilities(1, {"python"}, {});

    auto worker_opt4 = manager.get_worker(1);
    ASSERT_TRUE(worker_opt4.has_value());
    EXPECT_EQ(worker_opt4->get().capabilities_.size(), 1);
}

TEST(WorkerManagerTest, UpdateCapabilitiesRemoveNonexistent) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python"});

    manager.update_capabilities(1, {}, {"nonexistent"});

    auto worker_opt5 = manager.get_worker(1);
    ASSERT_TRUE(worker_opt5.has_value());
    EXPECT_EQ(worker_opt5->get().capabilities_.size(), 1);
}

TEST(WorkerManagerTest, UpdateCapabilitiesNonexistentWorker) {
    WorkerManager manager;
    manager.update_capabilities(999, {"gpu"}, {"python"});
    EXPECT_EQ(manager.get_worker_count(), 0);
}

TEST(WorkerManagerTest, HasWorkerWithAllCapabilitiesMatch) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python", "gpu"});
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"python"}));
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"gpu"}));
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"python", "gpu"}));
}

TEST(WorkerManagerTest, HasWorkerWithAllCapabilitiesNoMatch) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python"});
    EXPECT_FALSE(manager.has_worker_with_all_capabilities({"gpu"}));
    EXPECT_FALSE(manager.has_worker_with_all_capabilities({"python", "gpu"}));
}

TEST(WorkerManagerTest, HasWorkerWithAllCapabilitiesEmpty) {
    WorkerManager manager;
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({}));
}

TEST(WorkerManagerTest, HasWorkerWithAllCapabilitiesNoWorkers) {
    WorkerManager manager;
    EXPECT_FALSE(manager.has_worker_with_all_capabilities({"gpu"}));
}

TEST(WorkerManagerTest, HasWorkerWithAllCapabilitiesMultiWorker) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python"});
    manager.register_worker(2, "127.0.0.1", 8081, {"gpu"});
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"python"}));
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"gpu"}));
    EXPECT_FALSE(manager.has_worker_with_all_capabilities({"python", "gpu"}));
}

TEST(WorkerManagerTest, RecordHeartbeatNonExistent) {
    WorkerManager manager;
    EXPECT_NO_THROW(manager.record_heartbeat(999));
    EXPECT_EQ(manager.get_worker_count(), 0);
}

TEST(WorkerManagerTest, SetHeartbeatNonExistent) {
    WorkerManager manager;
    EXPECT_NO_THROW(manager.set_heartbeat(999, 12345));
    EXPECT_EQ(manager.get_worker_count(), 0);
}

TEST(WorkerManagerTest, SetHeartbeatOnExisting) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});

    manager.set_heartbeat(1, 99999);
    EXPECT_EQ(manager.get_worker(1)->get().last_heartbeat_, 99999);
}

TEST(WorkerManagerTest, AssignTaskNonExistent) {
    WorkerManager manager;
    EXPECT_NO_THROW(manager.assign_task(999, 100));
    EXPECT_EQ(manager.get_worker_count(), 0);
}

TEST(WorkerManagerTest, CompleteTaskNonExistent) {
    WorkerManager manager;
    EXPECT_NO_THROW(manager.complete_task(999));
    EXPECT_EQ(manager.get_worker_count(), 0);
}

TEST(WorkerManagerTest, UpdateWorkerStatusNonExistent) {
    WorkerManager manager;
    EXPECT_NO_THROW(manager.update_worker_status(999, WorkerStatus::BUSY));
    EXPECT_EQ(manager.get_worker_count(), 0);
}

TEST(WorkerManagerTest, UnregisterWorkerNonExistent) {
    WorkerManager manager;
    EXPECT_NO_THROW(manager.unregister_worker(999));
    EXPECT_EQ(manager.get_worker_count(), 0);
}

TEST(WorkerManagerTest, GetWorkerNonExistent) {
    WorkerManager manager;
    EXPECT_FALSE(manager.get_worker(999).has_value());
}

TEST(WorkerManagerTest, GetWorkersWithCapabilityEmpty) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python"});
    auto gpu_workers = manager.get_workers_with_capability("gpu");
    EXPECT_TRUE(gpu_workers.empty());
}

TEST(WorkerManagerTest, GetIdleWorkerCount) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});
    manager.register_worker(2, "127.0.0.1", 8081, {});
    EXPECT_EQ(manager.get_idle_worker_count(), 2);

    manager.assign_task(1, 100);
    EXPECT_EQ(manager.get_idle_worker_count(), 1);
}

TEST(WorkerManagerTest, ReRegisterWorkerOverwrites) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python"});
    manager.register_worker(1, "10.0.0.1", 9000, {"gpu"});

    EXPECT_EQ(manager.get_worker_count(), 1);
    auto worker_opt = manager.get_worker(1);
    ASSERT_TRUE(worker_opt.has_value());
    EXPECT_EQ(worker_opt->get().address_, "10.0.0.1");
    EXPECT_EQ(worker_opt->get().port_, 9000);
    EXPECT_EQ(worker_opt->get().capabilities_.size(), 1);
    EXPECT_EQ(worker_opt->get().capabilities_[0], "gpu");
}

TEST(WorkerManagerTest, UpdateCapabilitiesComplexScenario) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"a", "b", "c"});

    manager.update_capabilities(1, {"d"}, {"b"});

    auto worker_opt = manager.get_worker(1);
    ASSERT_TRUE(worker_opt.has_value());
    auto& caps = worker_opt->get().capabilities_;
    EXPECT_EQ(caps.size(), 3);

    bool has_a = false, has_c = false, has_d = false;
    for (const auto& cap : caps) {
        if (cap == "a") has_a = true;
        if (cap == "c") has_c = true;
        if (cap == "d") has_d = true;
    }
    EXPECT_TRUE(has_a);
    EXPECT_TRUE(has_c);
    EXPECT_TRUE(has_d);
}

TEST(WorkerManagerTest, HasWorkerWithAllCapabilitiesPartialMatch) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {"python", "gpu"});
    manager.register_worker(2, "127.0.0.1", 8081, {"python", "cuda"});

    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"python"}));
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"python", "gpu"}));
    EXPECT_TRUE(manager.has_worker_with_all_capabilities({"python", "cuda"}));
    EXPECT_FALSE(manager.has_worker_with_all_capabilities({"gpu", "cuda"}));
}

TEST(WorkerManagerTest, GetAllWorkersEmpty) {
    WorkerManager manager;
    auto all = manager.get_all_workers();
    EXPECT_TRUE(all.empty());
}

TEST(WorkerManagerTest, GetWorkersWithCapabilityNoWorkers) {
    WorkerManager manager;
    auto workers = manager.get_workers_with_capability("anything");
    EXPECT_TRUE(workers.empty());
}

TEST(WorkerManagerTest, CompleteTaskResetsWorkerState) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});

    manager.assign_task(1, 42);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::BUSY);
    EXPECT_EQ(manager.get_worker(1)->get().current_task_id_, 42);

    manager.complete_task(1);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::IDLE);
    EXPECT_EQ(manager.get_worker(1)->get().current_task_id_, 0);
    EXPECT_EQ(manager.get_idle_worker_count(), 1);
}

TEST(WorkerManagerTest, WorkerStatusTransitions) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});

    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::IDLE);

    manager.update_worker_status(1, WorkerStatus::BUSY);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::BUSY);

    manager.update_worker_status(1, WorkerStatus::DEAD);
    EXPECT_EQ(manager.get_worker(1)->get().status_, WorkerStatus::DEAD);
}

TEST(WorkerManagerTest, SetHeartbeatPersistsTimestamp) {
    WorkerManager manager;
    manager.register_worker(1, "127.0.0.1", 8080, {});

    manager.set_heartbeat(1, 12345);
    EXPECT_EQ(manager.get_worker(1)->get().last_heartbeat_, 12345);

    manager.set_heartbeat(1, 67890);
    EXPECT_EQ(manager.get_worker(1)->get().last_heartbeat_, 67890);
}

}  // namespace fly
