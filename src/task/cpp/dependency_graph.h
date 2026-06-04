#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <mutex>
#include <vector>

namespace fly {

class DependencyGraph {
public:
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs,
                  const CMVector<CMString>& required_capabilities = {});
    void mark_data_ready(const CMString& data_path);
    void mark_data_removed(const CMString& data_path);
    bool is_data_ready(const CMString& data_path) const;
    CMVector<uint64_t> get_ready_tasks() const;
    CMVector<uint64_t> get_pending_tasks() const;
    bool is_task_ready(uint64_t task_id) const;
    CMVector<CMString> get_task_requirements(uint64_t task_id) const;
    CMVector<CMString> get_task_dependencies(uint64_t task_id) const;
    void remove_task(uint64_t task_id);
    
private:
    CMUnorderedMap<uint64_t, CMVector<CMString>> task_dependencies_;
    CMUnorderedMap<CMString, bool> data_ready_status_;
    CMUnorderedMap<uint64_t, CMVector<CMString>> task_requirements_;
    CMUnorderedSet<uint64_t> ready_tasks_;
    CMUnorderedSet<uint64_t> pending_tasks_;
    CMUnorderedSet<uint64_t> completed_tasks_;
    mutable std::mutex mutex_;
};

}  // namespace fly