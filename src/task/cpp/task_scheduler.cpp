#include <task/cpp/task_scheduler.h>
#include <algorithm>

namespace fly {

TaskScheduler::TaskScheduler(DependencyGraph* graph, WorkerManager* manager)
    : graph_(graph), manager_(manager), locality_enabled_(true) {}

ScheduleResult TaskScheduler::schedule_next() {
    auto ready_tasks = graph_->get_ready_tasks();
    if (ready_tasks.empty()) {
        return {0, 0, false};
    }
    
    auto idle_workers = manager_->get_idle_workers();
    if (idle_workers.empty()) {
        return {0, 0, false};
    }
    
    for (uint64_t task_id : ready_tasks) {
        uint64_t worker_id = select_best_worker(task_id);
        if (worker_id == 0) continue;
        
        manager_->assign_task(worker_id, task_id);
        graph_->remove_task(task_id);
        
        return {task_id, worker_id, true};
    }
    
    return {0, 0, false};
}

CMVector<ScheduleResult> TaskScheduler::schedule_all_available() {
    CMVector<ScheduleResult> results;
    
    while (true) {
        auto result = schedule_next();
        if (!result.scheduled) {
            break;
        }
        results.push_back(result);
    }
    
    return results;
}

void TaskScheduler::set_locality_preference(bool enabled) {
    locality_enabled_ = enabled;
}

uint64_t TaskScheduler::select_best_worker(uint64_t task_id) {
    auto idle_workers = manager_->get_idle_workers();
    if (idle_workers.empty()) {
        return 0;
    }
    
    auto requirements = graph_->get_task_requirements(task_id);
    
    if (requirements.empty()) {
        return idle_workers[0];
    }
    
    CMVector<uint64_t> candidates;
    for (uint64_t wid : idle_workers) {
        auto* info = manager_->get_worker(wid);
        if (!info) continue;
        
        bool has_all = true;
        for (const auto& req : requirements) {
            bool found = false;
            for (const auto& cap : info->capabilities) {
                if (cap == req) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                has_all = false;
                break;
            }
        }
        if (has_all) {
            candidates.push_back(wid);
        }
    }
    
    if (candidates.empty()) {
        return 0;
    }
    
    return candidates[0];
}

}  // namespace fly
