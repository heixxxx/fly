#pragma once

#include <common/cpp/common_types.h>
#include <task/cpp/dependency_graph.h>
#include <task/cpp/worker_manager.h>
#include <cstdint>

namespace fly {

struct ScheduleResult {
    uint64_t task_id_;
    uint64_t worker_id_;
    bool scheduled_;
    bool degraded_ = false;  // 本次调度是否为降级调度（超时到期或 timeout==0）
};

class TaskScheduler {
public:
    TaskScheduler(DependencyGraph* graph, WorkerManager* manager);

    ScheduleResult schedule_next();
    CMVector<ScheduleResult> schedule_all_available();
    void set_locality_preference(bool enabled);
    bool locality_preference() const { return locality_enabled_; }

private:
    // 单次遍历 idle workers：完整匹配立即返回；无完整匹配时若 allow_degrade
    // 则返回匹配属性最多的 worker，否则返回 0（task 继续 waiting，不阻塞其他调度）。
    // idle_workers / idle_set 由调用方（schedule_next）一次性获取后传入复用，
    // 避免 select_best_worker 内部对每个 ready task 重复 get_idle_workers + 重建 set。
    uint64_t select_best_worker(uint64_t task_id, bool allow_degrade,
                                 const CMVector<uint64_t>& idle_workers,
                                 const CMUnorderedSet<uint64_t>& idle_set);
    // 为 task 计算各 worker 的 locality 分数，写入持久缓冲区 score_buf_（复用，无 per-task 分配）。
    // 返回填充的 entry 数（= 注册 worker 数）。
    size_t compute_scores(uint64_t task_id);

    DependencyGraph* graph_;
    WorkerManager* manager_;
    bool locality_enabled_ = false;

    // 持久复用的分数缓冲区：按 worker_id 直接索引（score_buf_[worker_id].score）。
    // score = worker 持有的输入数据总字节数（越大越优，0=未持有任何输入）。
    // 每次 compute_scores 就地重填，无 per-task 内存申请。worker_id 从 1 开始连续递增，
    // 故 score_buf_ 大小 = max_worker_id + 1（含空 slot 0，浪费可控）。
    struct ScoreEntry { uint64_t worker_id; int64_t score; };
    CMVector<ScoreEntry> score_buf_;
    // score 降序、worker_id 升序的比较器（阶段 B 选择数据亲和性最优 worker）。
    static bool score_desc_compare(const ScoreEntry& a, const ScoreEntry& b) {
        if (a.score != b.score) return a.score > b.score;  // score 大优先（持有输入多）
        return a.worker_id < b.worker_id;
    }
};

}  // namespace fly
