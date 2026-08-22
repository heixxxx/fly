#pragma once

#include <monitor/cpp/monitor_types.h>

#include <mutex>
#include <unordered_map>

namespace fly {

// TaskResourceTracker —— worker 侧 task 执行窗口资源归属（RSS avg/peak/baseline
// + 真实执行窗口 + 进程 CPU 时间）。
//
// 协作方：
//   · Python 执行线程（poll_task）：begin(task_id) / end(task_id) 包裹执行。
//   · monitor 采样线程（monitor_report_loop）：add_sample(rss) 每条样本计入
//     当前窗口；execute 返回后补入 IO 时刻峰值（事件驱动采样）。
//   · 上报路径（send_master_or_buffer）：take_agg(task_id) 取出结算并填消息
//     字段（取后清除，重复发送不再重复填）。
//
// begin/end 各含一次立即采样：短 task（< 采样间隔）也有 ≥2 个样本点；
// 配合 IO 事件采样（read 返回/write 前对象必存活）与执行期加密采样，
// 亚秒 task 的 avg/peak 不再依赖固定采样间隔撞运气。
// CPU 时间为 getrusage(RUSAGE_SELF) 的 utime+stime 微秒差分（窗口内全部
// 线程，含 reactor 等旁路线程的噪声——单进程模型下作为 IO/CPU 密集判别
// 口径足够；亚秒 task 亦有效，不受 jiffies 10ms 粒度限制）。
// 内部互斥保护（多线程并发访问），临界区仅数值累计，µs 级。
class TaskResourceTracker {
public:
    // 执行开始：记 baseline、窗口起点、CPU 基线并重置累计器。
    void begin(uint64_t task_id);

    // 执行结束：立即加采一条、结算 CPU 差分，结果存入 finished_（等 send
    // 路径取走）。task_id 不匹配当前窗口（重复 end / 异常路径）则丢弃窗口。
    void end(uint64_t task_id);

    // 周期/事件采样计入当前窗口（无窗口在跑则忽略）。
    void add_sample(uint64_t rss_bytes);

    // 取出某 task 的结算结果（取后清除；无结果返回 false——重复发送场景）。
    bool take_agg(uint64_t task_id, TaskResourceAgg& out);

    // 当前是否有执行窗口打开（monitor 线程据此做执行期加密采样）。
    bool has_active() const;

private:
    void add_sample_locked(uint64_t rss_bytes);

    struct Window {
        uint64_t task_id_ = 0;
        uint64_t exec_start_ms_ = 0;
        int64_t begin_cpu_usec_ = 0;   // getrusage utime+stime（微秒）
        uint64_t baseline_ = 0;
        uint64_t sum_ = 0;
        uint32_t count_ = 0;
        uint64_t peak_ = 0;
    };

    mutable std::mutex mutex_;
    Window current_;                                    // begin/end 驱动
    std::unordered_map<uint64_t, TaskResourceAgg> finished_;  // end → send 暂存
};

}  // namespace fly
