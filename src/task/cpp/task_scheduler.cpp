#include <task/cpp/task_scheduler.h>
#include <log/cpp/logger.h>
#include <algorithm>
#include <chrono>

namespace fly {

TaskScheduler::TaskScheduler(DependencyGraph* graph, WorkerManager* manager)
    : graph_(graph), manager_(manager) {}

ScheduleResult TaskScheduler::schedule_next() {
    auto ready_tasks = graph_->get_ready_tasks();
    if (ready_tasks.empty()) {
        return {0, 0, false, false};
    }

    auto idle_workers = manager_->get_idle_workers();
    if (idle_workers.empty()) {
        return {0, 0, false, false};
    }

    auto now = std::chrono::steady_clock::now();

    for (uint64_t task_id : ready_tasks) {
        auto reqs = graph_->get_task_requirements(task_id);
        bool allow_degrade = false;

        if (reqs.timeout_seconds_ >= 0.0f) {
            // timeout >= 0：判断是否允许降级
            if (reqs.timeout_seconds_ == 0.0f) {
                allow_degrade = true;  // 立即降级（仅检查一次）
            } else {
                auto ready_time = graph_->get_task_ready_timestamp(task_id);
                if (ready_time.has_value()) {
                    double elapsed =
                        std::chrono::duration<double>(now - ready_time.value()).count();
                    if (elapsed >= reqs.timeout_seconds_) {
                        allow_degrade = true;  // 已超时，允许降级
                    }
                }
            }
        }
        // timeout < 0（死等）：allow_degrade 保持 false

        uint64_t worker_id = select_best_worker(task_id, allow_degrade);
        if (worker_id == 0) continue;  // waiting，不阻塞后续 task 调度

        // 判断是否为降级调度：允许降级且 worker 非完整匹配
        bool degraded = false;
        if (allow_degrade && !reqs.capabilities_.empty()) {
            auto info_opt = manager_->get_worker(worker_id);
            if (info_opt.has_value()) {
                auto& info = info_opt->get();
                size_t match_count = 0;
                for (const auto& req : reqs.capabilities_) {
                    for (const auto& cap : info.capabilities_) {
                        if (cap == req) { match_count++; break; }
                    }
                }
                degraded = (match_count < reqs.capabilities_.size());
            }
        }

        manager_->assign_task(worker_id, task_id);
        graph_->remove_task(task_id);

        if (degraded) {
            DBG("[SCHED] task={} degraded-scheduled to worker={} (timeout={})",
                task_id, worker_id, reqs.timeout_seconds_);
        }
        return {task_id, worker_id, true, degraded};
    }

    return {0, 0, false, false};
}

CMVector<ScheduleResult> TaskScheduler::schedule_all_available() {
    CMVector<ScheduleResult> results;

    while (true) {
        auto result = schedule_next();
        if (!result.scheduled_) {
            break;
        }
        results.push_back(result);
    }

    return results;
}

void TaskScheduler::set_locality_preference(bool /*enabled*/) {
}

uint64_t TaskScheduler::select_best_worker(uint64_t task_id, bool allow_degrade) {
    auto idle_workers = manager_->get_idle_workers();
    if (idle_workers.empty()) {
        return 0;
    }

    auto reqs = graph_->get_task_requirements(task_id);
    if (reqs.capabilities_.empty()) {
        return idle_workers[0];
    }

    // 单次遍历：完整匹配立即返回；顺便记录最佳部分匹配（降级时使用）
    uint64_t best_partial_worker = 0;
    size_t best_partial_count = 0;

    for (uint64_t wid : idle_workers) {
        auto info_opt = manager_->get_worker(wid);
        if (!info_opt) continue;
        auto& info = info_opt->get();

        size_t match_count = 0;
        for (const auto& req : reqs.capabilities_) {
            bool found = false;
            for (const auto& cap : info.capabilities_) {
                if (cap == req) { found = true; break; }
            }
            if (found) match_count++;
        }

        // 完整匹配：立即返回，无需继续遍历
        if (match_count == reqs.capabilities_.size()) {
            return wid;
        }
        // 顺便记录匹配属性最多的 worker（降级时使用）
        if (allow_degrade && match_count > best_partial_count) {
            best_partial_count = match_count;
            best_partial_worker = wid;
        }
    }

    // 无完整匹配：允许降级则返回最佳部分匹配（匹配数可能为 0，此时回退到第一个 idle worker）
    if (allow_degrade) {
        return best_partial_worker != 0 ? best_partial_worker : idle_workers[0];
    }
    return 0;
}

}  // namespace fly
