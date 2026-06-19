#include <task/cpp/dependency_graph.h>
#include <log/cpp/logger.h>
#include <algorithm>

namespace fly {

void DependencyGraph::add_task(uint64_t task_id, const CMVector<CMString>& inputs,
                                const CMVector<CMString>& required_capabilities) {
    std::lock_guard<std::mutex> lock(mutex_);
    task_dependencies_[task_id] = inputs;
    task_requirements_[task_id] = required_capabilities;

    int pending = 0;
    for (const auto& dep : inputs) {
        if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
            pending++;
            // Build reverse index: dep → task_id
            data_to_pending_tasks_[dep].insert(task_id);
        }
    }

    if (pending == 0) {
        ready_tasks_.insert(task_id);
    } else {
        pending_tasks_.insert(task_id);
    }
    DBG("[DEP] add_task: id={} deps={} pending_deps={} → {}", task_id, inputs.size(), pending, pending == 0 ? "READY" : "PENDING");
}

bool DependencyGraph::check_and_move_to_ready(uint64_t task_id) {
    // Called under lock. Returns true if task moved from pending to ready.
    if (completed_tasks_.count(task_id)) return false;
    if (ready_tasks_.count(task_id)) return false;
    if (!pending_tasks_.count(task_id)) return false;

    auto& deps = task_dependencies_[task_id];
    for (const auto& dep : deps) {
        if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
            return false;  // still has unmet deps
        }
    }

    // All deps ready — move to ready.
    pending_tasks_.erase(task_id);
    ready_tasks_.insert(task_id);

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
    CMVector<uint64_t> to_pending;
    for (auto task_id : ready_tasks_) {
        auto& deps = task_dependencies_[task_id];
        for (const auto& dep : deps) {
            if (dep == data_path) {
                to_pending.push_back(task_id);
                break;
            }
        }
    }

    for (auto task_id : to_pending) {
        ready_tasks_.erase(task_id);
        pending_tasks_.insert(task_id);
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
    CMVector<uint64_t> result(ready_tasks_.begin(), ready_tasks_.end());
    std::sort(result.begin(), result.end());
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
    return ready_tasks_.count(task_id) > 0;
}

void DependencyGraph::remove_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    ready_tasks_.erase(task_id);
    pending_tasks_.erase(task_id);
    completed_tasks_.insert(task_id);

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

CMVector<CMString> DependencyGraph::get_task_requirements(uint64_t task_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = task_requirements_.find(task_id);
    if (it != task_requirements_.end()) {
        return it->second;
    }
    return {};
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
