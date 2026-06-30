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
        const auto& reqs = graph_->get_task_requirements(task_id);
        float timeout = reqs.timeout_seconds_;
        bool allow_degrade = false;

        if (timeout >= 0.0f) {
            // timeout >= 0：判断是否允许降级
            if (timeout == 0.0f) {
                allow_degrade = true;  // 立即降级（仅检查一次）
            } else {
                auto ready_time = graph_->get_task_ready_timestamp(task_id);
                if (ready_time.has_value()) {
                    double elapsed =
                        std::chrono::duration<double>(now - ready_time.value()).count();
                    if (elapsed >= timeout) {
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
                task_id, worker_id, timeout);
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

void TaskScheduler::set_locality_preference(bool enabled) {
    locality_enabled_ = enabled;
}

// 计算 worker 对 required capabilities 的匹配数。
static size_t capability_match_count(WorkerManager* manager, uint64_t wid,
                                     const CMVector<CMString>& reqs) {
    auto info_opt = manager->get_worker(wid);
    if (!info_opt) return 0;
    auto& info = info_opt->get();
    size_t match_count = 0;
    for (const auto& req : reqs) {
        for (const auto& cap : info.capabilities_) {
            if (cap == req) { match_count++; break; }
        }
    }
    return match_count;
}

// 为 task 计算各 worker 的 locality 分数，写入持久缓冲区 score_buf_（复用，无 per-task 分配）。
// score_buf_ 按 worker_id 直接索引（下标=worker_id）。score = worker 持有的输入数据总字节数
// （越大越优，0=未持有任何输入）。
//
// scheduler 不接触 DataService：直接消费 master 预计算的 locality_hint_（POD）。
// hint 每个 entry = (worker_id, 该 worker 持有的输入字节数)，master 已聚合完毕，直接赋值。
size_t TaskScheduler::compute_scores(uint64_t task_id) {
    auto all_workers = manager_->get_all_workers();

    // 算出 max_worker_id，按 worker_id 直接索引（下标=worker_id）。容量只增不减。
    uint64_t max_id = 0;
    for (const auto& w : all_workers) {
        if (w.worker_id_ > max_id) max_id = w.worker_id_;
    }
    score_buf_.clear();
    score_buf_.resize(max_id + 1);
    for (const auto& w : all_workers) {
        score_buf_[w.worker_id_] = {w.worker_id_, 0};
    }

    // 消费 master 预计算的 locality_hint_（POD）。master 是数据位置权威，
    // 在 schedule_tasks() 入口按 task 依赖查 DataService 预聚合后注入。
    // hint 为空（无输入对象 / 未注入）→ 所有 score 保持 0，退原行为（按 worker_id 升序选）。
    const TaskRequirements& reqs = graph_->get_task_requirements(task_id);
    for (const auto& [wid, score] : reqs.locality_hint_) {
        if (wid < score_buf_.size()) {
            score_buf_[wid].score = score;  // master 已聚合，直接赋值
        }
    }
    return score_buf_.size();
}

uint64_t TaskScheduler::select_best_worker(uint64_t task_id, bool allow_degrade) {
    auto idle_workers = manager_->get_idle_workers();
    if (idle_workers.empty()) {
        return 0;
    }

    // idle_workers 转 set 便于 O(1) 查询（避免 locality 阶段反复线性查找）
    CMUnorderedSet<uint64_t> idle_set(idle_workers.begin(), idle_workers.end());

    const TaskRequirements& reqs = graph_->get_task_requirements(task_id);
    const CMVector<CMString>& caps = reqs.capabilities_;

    // 仅在 locality 启用时算分。caps 为空时也走 locality（无 capability 约束，纯按数据亲和选 worker）。
    bool use_locality = locality_enabled_;
    if (use_locality) {
        compute_scores(task_id);
    }

    if (caps.empty()) {
        // 无 capability 要求：阶段 B locality 偏好（按 score 升序选首个 idle 的）。
        if (use_locality && !score_buf_.empty()) {
            std::sort(score_buf_.begin(), score_buf_.end(), score_desc_compare);
            for (const auto& entry : score_buf_) {
                if (idle_set.count(entry.worker_id)) {
                    return entry.worker_id;
                }
            }
        }
        return idle_workers[0];
    }

    // 阶段 A：capability 完整匹配立即返回；顺便记录全局最佳部分匹配。
    uint64_t best_partial_worker = 0;
    size_t best_partial_count = 0;
    for (uint64_t wid : idle_workers) {
        size_t match_count = capability_match_count(manager_, wid, caps);
        if (match_count == caps.size()) {
            return wid;  // 完整匹配，强约束优先
        }
        if (match_count > best_partial_count) {
            best_partial_count = match_count;
            best_partial_worker = wid;
        }
    }

    // 阶段 B：locality 偏好（仅 use_locality）。
    // 不变量：选中的 worker 的 capability 匹配数不得低于全局最佳部分匹配数，
    // 且必须有至少部分匹配（best_partial_count > 0）—— 无任何 worker 匹配时不调度（交阶段 C/waiting），
    // 避免 locality 绕过 capability 死锁检测。
    if (use_locality && !score_buf_.empty() && best_partial_count > 0) {
        // 按 score 降序遍历，找 score 最大（持有输入最多）且 capability 不降级的 idle worker。
        std::sort(score_buf_.begin(), score_buf_.end(), score_desc_compare);
        for (const auto& entry : score_buf_) {
            if (!idle_set.count(entry.worker_id)) continue;
            size_t match_count = capability_match_count(manager_, entry.worker_id, caps);
            if (match_count >= best_partial_count) {
                return entry.worker_id;
            }
        }
    }

    // 阶段 C：兜底（原 allow_degrade 逻辑）。
    if (allow_degrade) {
        return best_partial_worker != 0 ? best_partial_worker : idle_workers[0];
    }
    return 0;
}

}  // namespace fly
