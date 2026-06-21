#pragma once

#include <common/cpp/common_types.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <cstdint>

namespace fly {

struct ScheduleResult {
    uint64_t task_id_;
    uint64_t worker_id_;
    bool scheduled_;
    bool degraded_ = false;  // 本次调度是否为降级调度（超时到期或 timeout==0）
};

class TaskScheduler {
public:
    TaskScheduler(DependencyGraph* graph, WorkerManager* manager);

    ScheduleResult schedule_next();
    CMVector<ScheduleResult> schedule_all_available();
    void set_locality_preference(bool enabled);

private:
    // 单次遍历 idle workers：完整匹配立即返回；无完整匹配时若 allow_degrade
    // 则返回匹配属性最多的 worker，否则返回 0（task 继续 waiting，不阻塞其他调度）
    uint64_t select_best_worker(uint64_t task_id, bool allow_degrade);

    DependencyGraph* graph_;
    WorkerManager* manager_;
};

}  // namespace fly
