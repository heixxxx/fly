#pragma once

#include <common/cpp/common_types.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <cstdint>

namespace fly {

struct ScheduleResult {
    uint64_t task_id;
    uint64_t worker_id;
    bool scheduled;
};

class TaskScheduler {
public:
    TaskScheduler(DependencyGraph* graph, WorkerManager* manager);
    
    ScheduleResult schedule_next();
    CMVector<ScheduleResult> schedule_all_available();
    void set_locality_preference(bool enabled);
    
private:
    uint64_t select_best_worker(uint64_t task_id);
    
    DependencyGraph* graph_;
    WorkerManager* manager_;
};

}  // namespace fly