#pragma once

#include <common/cpp/common_types.h>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

namespace fly {

// TaskRequirements 描述一个 task 的 worker 属性依赖。
// timeout_seconds_ 取值语义：
//   <0  死等，必须满足 capabilities_ 才调度（默认值，纯 list 形式的等价语义）
//   ==0 数据依赖满足后仅检查一次，无完整匹配立即降级到匹配属性最多的 idle worker
//   >0 数据依赖满足后限时等待；到期后无论属性是否满足，都降级调度
struct TaskRequirements {
    CMVector<CMString> capabilities_;
    float timeout_seconds_ = -1.0f;

    // Locality hint：master 预计算的 worker→亲和分（worker 持有的输入字节数）。
    // scheduler 只消费此 POD，不接触 DataService，从而解除 task→storage 的分层依赖。
    // 空 = 无 locality 信息（退 FIFO）。纯进程内临场数据，不参与序列化
    // （TaskRequirements 不跨进程；跨进程消息用独立字段，见 message_types.h）。
    // 每个 entry = (worker_id, 该 worker 持有的输入数据总字节数)。
    CMVector<std::pair<uint64_t, int64_t>> locality_hint_{};

    // 任务优先级：数值越大越优先调度。默认 10（中点值，可双向调节：<10 让路，>10 抢先）。
    // 仅影响 ready task 的调度顺序（get_ready_tasks 按 priority 降序 + task_id 升序排）。
    // head-of-line skip：高优先级 task 若无可匹配 worker，跳过它调度低优先级（不阻塞）。
    int priority_ = 10;
};

class DependencyGraph {
public:
    void add_task(uint64_t task_id, const CMVector<CMString>& inputs,
                  const TaskRequirements& requirements = {});
    // master 在 schedule 前按 task 依赖查 DataService 预计算 locality_hint_，写入此结构。
    // scheduler 只读不查。线程安全（内部加锁，与其它 getter 一致）。
    // task 不存在时静默忽略（与 get_task_requirements 找不到返回静态空的语义对称）。
    void set_task_locality_hint(uint64_t task_id,
                                CMVector<std::pair<uint64_t, int64_t>> hint);
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
    // ready_tasks_ 用有序 set 维护，key = {-priority, task_id}：
    //   - -priority 升序 = priority 降序（高优先级在前）
    //   - 同 priority 内 task_id 升序（FIFO）
    // 插入即有序，get_ready_tasks() 无需每次 std::sort（消除 H2-b 的 O(N²) 退化）。
    // priority 在 task 生命周期内不可变（add_task 时确定，无运行时 set_priority），
    // 故 key 稳定，插入后无需更新。
    std::set<std::pair<int, uint64_t>> ready_tasks_;
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
