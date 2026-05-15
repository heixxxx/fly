#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace fly {

class DependencyGraph {
public:
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs);
    void mark_data_ready(const CMString& data_path);
    CMVector<uint64_t> get_ready_tasks();
    bool is_task_ready(uint64_t task_id);
    void remove_task(uint64_t task_id);
    
private:
    CMMap<uint64_t, CMVector<CMString>> task_dependencies_;
    CMMap<CMString, bool> data_ready_status_;
    CMMap<uint64_t, int> pending_count_;
    CMSet<uint64_t> ready_tasks_;
    CMSet<uint64_t> completed_tasks_;
};

}  // namespace fly