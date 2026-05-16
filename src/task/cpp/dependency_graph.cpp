#include <task/cpp/dependency_graph.h>
#include <algorithm>

namespace fly {

void DependencyGraph::add_task(uint64_t task_id, const CMVector<CMString>& inputs) {
    task_dependencies_[task_id] = inputs;
    
    int pending = 0;
    for (const auto& dep : inputs) {
        if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
            pending++;
        }
    }
    
    pending_count_[task_id] = pending;
    
    if (pending == 0) {
        ready_tasks_.insert(task_id);
    } else {
        pending_tasks_.insert(task_id);
    }
}

void DependencyGraph::mark_data_ready(const CMString& data_path) {
    data_ready_status_[data_path] = true;
    
    CMVector<uint64_t> to_ready;
    
    for (auto& task_id : pending_tasks_) {
        if (completed_tasks_.count(task_id)) continue;
        if (ready_tasks_.count(task_id)) continue;
        
        auto& deps = task_dependencies_[task_id];
        bool all_ready = true;
        for (const auto& dep : deps) {
            if (!data_ready_status_.count(dep) || !data_ready_status_[dep]) {
                all_ready = false;
                break;
            }
        }
        
        if (all_ready) {
            to_ready.push_back(task_id);
        }
    }
    
    for (auto task_id : to_ready) {
        pending_tasks_.erase(task_id);
        ready_tasks_.insert(task_id);
    }
}

CMVector<uint64_t> DependencyGraph::get_ready_tasks() const {
    CMVector<uint64_t> result(ready_tasks_.begin(), ready_tasks_.end());
    return result;
}

CMVector<uint64_t> DependencyGraph::get_pending_tasks() const {
    CMVector<uint64_t> result(pending_tasks_.begin(), pending_tasks_.end());
    return result;
}

bool DependencyGraph::is_task_ready(uint64_t task_id) const {
    return ready_tasks_.count(task_id) > 0;
}

void DependencyGraph::remove_task(uint64_t task_id) {
    ready_tasks_.erase(task_id);
    pending_tasks_.erase(task_id);
    completed_tasks_.insert(task_id);
    task_dependencies_.erase(task_id);
    pending_count_.erase(task_id);
}

}  // namespace fly