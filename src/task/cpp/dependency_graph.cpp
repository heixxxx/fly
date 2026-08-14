#include <task/cpp/dependency_graph.h>
#include <log/cpp/logger.h>
#include <algorithm>
#include <cassert>

namespace fly {

void DependencyGraph::add_task(uint64_t task_id, const CMVector<CMString>& inputs,
                                const TaskRequirements& requirements) {
    std::lock_guard<std::mutex> lock(mutex_);

    // add_task 语义为"新建"：task_id 必须唯一。重复 add 不会清理旧的
    // ready_tasks_/pending_tasks_/data_to_pending_tasks_ 反向索引，导致 graph 内部
    // 状态分叉（与 TaskManager::create_task 的 assert 对称）。rerun/task 恢复路径
    // 必须 remove_task 在前（见 on_disconnect/schedule_tasks 的 remove+add 序列）。
    // 命中此 assert = task_id 复用或跨线程竞态，立即崩溃暴露现场。
    if (task_dependencies_.count(task_id) > 0) {
        ERR("[FATAL] DependencyGraph::add_task: duplicate task_id={} — "
            "must remove_task before re-adding. Aborting to expose the race.",
            task_id);
        assert(false && "add_task: duplicate task_id");
    }

    task_dependencies_[task_id] = inputs;
    task_requirements_[task_id] = requirements;

    int pending = 0;
    for (const auto& dep : inputs) {
        if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
            pending++;
            // Build reverse index: dep → task_id
            data_to_pending_tasks_[dep].insert(task_id);
        }
    }

    if (pending == 0) {
        ready_tasks_.insert({-requirements.priority_, task_id});
        task_ready_timestamps_[task_id] = std::chrono::steady_clock::now();
    } else {
        pending_tasks_.insert(task_id);
    }
    DBG("[DEP] add_task: id={} deps={} pending_deps={} timeout={} → {}",
        task_id, inputs.size(), pending, requirements.timeout_seconds_,
        pending == 0 ? "READY" : "PENDING");
}

void DependencyGraph::set_task_locality_hint(
        uint64_t task_id, CMVector<std::pair<uint64_t, int64_t>> hint) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_requirements_.find(task_id);
    if (it != task_requirements_.end()) {
        it->second.locality_hint_ = std::move(hint);
    }
    // task 不存在时静默忽略：set 可能在 add_task 之前调用（防御），
    // 与 get_task_requirements 的"找不到返回静态空"语义对称。
}

bool DependencyGraph::check_and_move_to_ready(uint64_t task_id) {
    // Called under lock. Returns true if task moved from pending to ready.
    if (completed_tasks_.count(task_id)) return false;
    // ready_tasks_ key 含 priority，此处只需判 task 是否已在 ready。
    // 用 find_if 按 task_id 匹配（避免查 priority；此分支命中即 return，非热路径）。
    bool already_ready = false;
    for (const auto& entry : ready_tasks_) {
        if (entry.second == task_id) { already_ready = true; break; }
    }
    if (already_ready) return false;
    if (!pending_tasks_.count(task_id)) return false;

    auto& deps = task_dependencies_[task_id];
    for (const auto& dep : deps) {
        if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
            return false;  // still has unmet deps
        }
    }

    // All deps ready — move to ready.
    pending_tasks_.erase(task_id);
    int priority = 10;  // 默认值，与 get_ready_tasks 排序比较器的默认一致
    auto req_it = task_requirements_.find(task_id);
    if (req_it != task_requirements_.end()) priority = req_it->second.priority_;
    ready_tasks_.insert({-priority, task_id});
    task_ready_timestamps_[task_id] = std::chrono::steady_clock::now();

    // Clean up reverse index for this task.
    for (const auto& dep : deps) {
        auto it = data_to_pending_tasks_.find(dep);
        if (it != data_to_pending_tasks_.end()) {
            it->second.erase(task_id);
            if (it->second.empty()) {
                data_to_pending_tasks_.erase(it);
            }
        }
    }

    return true;
}

void DependencyGraph::mark_data_ready(const CMString& data_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_ready_status_[data_path] = true;

    DBG("[DEP] mark_data_ready: data={} pending_count={}", data_path, pending_tasks_.size());

    // Only check tasks that depend on this data (O(T×D) instead of O(P×D)).
    auto idx_it = data_to_pending_tasks_.find(data_path);
    if (idx_it == data_to_pending_tasks_.end()) return;

    // Copy the set because check_and_move_to_ready modifies it.
    auto affected_tasks = idx_it->second;
    for (auto task_id : affected_tasks) {
        if (check_and_move_to_ready(task_id)) {
            DBG("[DEP] mark_data_ready: task={} → READY", task_id);
        }
    }
}

void DependencyGraph::mark_data_removed(const CMString& data_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_ready_status_.erase(data_path);

    // Find ready tasks that depend on this data and move them back to pending.
    // ready_tasks_ 的 key 是 {-priority, task_id}，遍历取 .second 得 task_id。
    CMVector<uint64_t> to_pending;
    for (const auto& entry : ready_tasks_) {
        uint64_t task_id = entry.second;
        auto& deps = task_dependencies_[task_id];
        for (const auto& dep : deps) {
            if (dep == data_path) {
                to_pending.push_back(task_id);
                break;
            }
        }
    }

    for (auto task_id : to_pending) {
        // erase 需用完整 key：从 task_requirements_ 查 priority 构造 {-priority, task_id}。
        int priority = 10;
        auto req_it = task_requirements_.find(task_id);
        if (req_it != task_requirements_.end()) priority = req_it->second.priority_;
        ready_tasks_.erase({-priority, task_id});
        pending_tasks_.insert(task_id);
        task_ready_timestamps_.erase(task_id);
        // Re-add to reverse index for all unmet deps.
        for (const auto& dep : task_dependencies_[task_id]) {
            if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
                data_to_pending_tasks_[dep].insert(task_id);
            }
        }
    }
}

CMVector<uint64_t> DependencyGraph::get_ready_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // ready_tasks_ 已按 {-priority, task_id} 有序（priority 降序、同优先级 task_id 升序），
    // 直接遍历取 task_id，无需 std::sort。原实现每次 O(R log R) sort 导致 schedule_all_available
    // 循环呈 O(N²) 退化（见 docs/perf-baseline-scheduling-hotloop.md）。
    CMVector<uint64_t> result;
    result.reserve(ready_tasks_.size());
    for (const auto& entry : ready_tasks_) {
        result.push_back(entry.second);
    }
    return result;
}

bool DependencyGraph::is_data_ready(const CMString& data_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_ready_status_.find(data_path);
    return it != data_ready_status_.end() && it->second;
}

CMVector<uint64_t> DependencyGraph::get_pending_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    CMVector<uint64_t> result(pending_tasks_.begin(), pending_tasks_.end());
    std::sort(result.begin(), result.end());
    return result;
}

bool DependencyGraph::is_task_ready(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // ready_tasks_ key 是 {-priority, task_id}，需查 priority 构造完整 key。
    int priority = 10;
    auto req_it = task_requirements_.find(task_id);
    if (req_it != task_requirements_.end()) priority = req_it->second.priority_;
    return ready_tasks_.count({-priority, task_id}) > 0;
}

void DependencyGraph::remove_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    // ready_tasks_ 的 key 是 {-priority, task_id}，erase 需完整 key。
    // task_requirements_ 在本方法末尾才 erase，此时仍可查 priority。
    int priority = 10;
    auto req_it = task_requirements_.find(task_id);
    if (req_it != task_requirements_.end()) priority = req_it->second.priority_;
    ready_tasks_.erase({-priority, task_id});
    pending_tasks_.erase(task_id);
    completed_tasks_.insert(task_id);
    task_ready_timestamps_.erase(task_id);

    // Clean up reverse index.
    auto dep_it = task_dependencies_.find(task_id);
    if (dep_it != task_dependencies_.end()) {
        for (const auto& dep : dep_it->second) {
            auto idx_it = data_to_pending_tasks_.find(dep);
            if (idx_it != data_to_pending_tasks_.end()) {
                idx_it->second.erase(task_id);
                if (idx_it->second.empty()) {
                    data_to_pending_tasks_.erase(idx_it);
                }
            }
        }
    }

    task_dependencies_.erase(task_id);
    task_requirements_.erase(task_id);
}

size_t DependencyGraph::completed_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completed_tasks_.size();
}

TaskRequirements DependencyGraph::get_task_requirements(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_requirements_.find(task_id);
    if (it != task_requirements_.end()) {
        return it->second;  // 拷贝快照，锁内完成
    }
    return TaskRequirements{};
}

std::optional<std::chrono::steady_clock::time_point>
DependencyGraph::get_task_ready_timestamp(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_ready_timestamps_.find(task_id);
    if (it != task_ready_timestamps_.end()) {
        return it->second;
    }
    return std::nullopt;
}

CMVector<CMString> DependencyGraph::get_task_dependencies(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_dependencies_.find(task_id);
    if (it != task_dependencies_.end()) {
        return it->second;
    }
    return {};
}

}  // namespace fly
