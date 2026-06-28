#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <optional>
#include <vector>

namespace fly {

// TaskRequirements 描述一个 task 的 worker 属性依赖。
// timeout_seconds_ 取值语义：
//   <0  死等，必须满足 capabilities_ 才调度（默认值，纯 list 形式的等价语义）
//   ==0 数据依赖满足后仅检查一次，无完整匹配立即降级到匹配属性最多的 idle worker
//   >0  数据依赖满足后限时等待；到期后无论属性是否满足，都降级调度
struct TaskRequirements {
    CMVector<CMString> capabilities_;
    float timeout_seconds_ = -1.0f;
};

class DependencyGraph {
public:
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs,
                  const TaskRequirements& requirements = {});
    void mark_data_ready(const CMString& data_path);
    void mark_data_removed(const CMString& data_path);
    bool is_data_ready(const CMString& data_path) const;
    CMVector<uint64_t> get_ready_tasks() const;
    CMVector<uint64_t> get_pending_tasks() const;
    bool is_task_ready(uint64_t task_id) const;
    // 返回 task requirements 的 const 引用（无值拷贝）。找不到时返回静态空对象
    // （空 capabilities + 默认 timeout=-1），调用方依赖此默认值语义。
    const TaskRequirements& get_task_requirements(uint64_t task_id) const;
    // 返回 task 进入 ready 的时间点（用于 attribute timeout 判断）
    std::optional<std::chrono::steady_clock::time_point>
    get_task_ready_timestamp(uint64_t task_id) const;
    CMVector<CMString> get_task_dependencies(uint64_t task_id) const;
    void remove_task(uint64_t task_id);

private:
    bool check_and_move_to_ready(uint64_t task_id);

    CMUnorderedMap<uint64_t, CMVector<CMString>> task_dependencies_;
    CMUnorderedMap<CMString, bool> data_ready_status_;
    CMUnorderedMap<uint64_t, TaskRequirements> task_requirements_;
    CMUnorderedSet<uint64_t> ready_tasks_;
    CMUnorderedSet<uint64_t> pending_tasks_;
    CMUnorderedSet<uint64_t> completed_tasks_;

    // task 进入 ready 的时间点（pending→ready 转换时记录），用于
    // attribute timeout 判断。remove_task 时清理。
    CMUnorderedMap<uint64_t, std::chrono::steady_clock::time_point> task_ready_timestamps_;

    // Reverse index: data_path → set of pending task_ids that depend on it.
    // Avoids iterating all pending tasks in mark_data_ready().
    CMUnorderedMap<CMString, CMUnorderedSet<uint64_t>> data_to_pending_tasks_;

    mutable std::mutex mutex_;
};

}  // namespace fly
