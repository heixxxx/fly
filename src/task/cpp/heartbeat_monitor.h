#pragma once

#include <common/types/cpp/pointer_aliases.h>
#include <container/cpp/container_aliases.h>
#include <task/cpp/worker_manager.h>
#include <cstdint>

namespace fly {

// HeartbeatMonitor —— 对 WorkerManager **弱观察**（CMWeakPtr，§16 判据：宿主
// MasterAgent start() 会整体重建 worker_manager，裸指针观察在重建窗口悬垂）。
// 宿主以 CMSharedPtr 持有 manager；检查入口 lock，lock 失败 = 关停/重建窗口，
// 本轮检查直接跳过（无 worker 可判死）。
class HeartbeatMonitor {
public:
    HeartbeatMonitor(CMSharedPtr<WorkerManager> manager, uint64_t timeout_seconds = 30);

    // exempt_workers：断连宽限中的 worker（心跳缺失由宽限计时器统一判定，豁免
    // 心跳超时检查，防 120s 心跳与宽限窗口撞车抢跑）。
    void check_all_workers(uint64_t current_time,
                           const CMVector<uint64_t>& exempt_workers = {});
    uint64_t get_timeout() const;
    void set_timeout(uint64_t seconds);
    CMVector<uint64_t> get_dead_workers() const;

private:
    CMWeakPtr<WorkerManager> manager_;
    uint64_t timeout_seconds_;
    CMVector<uint64_t> dead_workers_;
};

}  // namespace fly
