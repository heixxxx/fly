#include <monitor/cpp/task_resource_tracker.h>
#include <core/cpp/system_info.h>

#include <sys/resource.h>
#include <unistd.h>

namespace fly {

namespace {
// 进程 CPU 时间（微秒，utime+stime 全线程和）。getrusage 微秒精度——亚秒
// task 的 cpu_time 不受 /proc jiffies 10ms 粒度限制（短 task 恒 0 的根治）。
int64_t cpu_usec_now() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;
    return ru.ru_utime.tv_sec * 1000000LL + ru.ru_utime.tv_usec +
           ru.ru_stime.tv_sec * 1000000LL + ru.ru_stime.tv_usec;
}
}  // namespace

void TaskResourceTracker::begin(uint64_t task_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    current_ = Window{};
    current_.task_id_ = task_id;
    current_.exec_start_ms_ = monitor_epoch_ms_now();
    current_.begin_cpu_usec_ = cpu_usec_now();
    current_.baseline_ = SystemInfo::process_rss_bytes();
    current_.sum_ = current_.baseline_;
    current_.count_ = 1;
    current_.peak_ = current_.baseline_;
}

void TaskResourceTracker::add_sample_locked(uint64_t rss_bytes) {
    if (current_.task_id_ == 0 || rss_bytes == 0) return;
    current_.sum_ += rss_bytes;
    current_.count_++;
    if (rss_bytes > current_.peak_) current_.peak_ = rss_bytes;
}

void TaskResourceTracker::add_sample(uint64_t rss_bytes) {
    std::lock_guard<std::mutex> lk(mutex_);
    add_sample_locked(rss_bytes);
}

bool TaskResourceTracker::has_active() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return current_.task_id_ != 0;
}

void TaskResourceTracker::end(uint64_t task_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (current_.task_id_ != task_id || current_.task_id_ == 0) {
        current_ = Window{};  // 异常路径：丢弃错配窗口，防御脏状态
        return;
    }
    add_sample_locked(SystemInfo::process_rss_bytes());

    TaskResourceAgg agg;
    agg.exec_start_ms_ = current_.exec_start_ms_;
    agg.exec_end_ms_ = monitor_epoch_ms_now();
    const int64_t cpu_now = cpu_usec_now();
    if (cpu_now > 0 && current_.begin_cpu_usec_ > 0 &&
        cpu_now >= current_.begin_cpu_usec_) {
        // ceil 取整：亚毫秒窗口记 1ms 而非 0（同 io_stats 亚毫秒 IO 计时
        // 的裁定口径——快机/高负载下短 task 的微秒差分向下取整会恒 0）。
        agg.cpu_time_ms_ = static_cast<uint64_t>(
            (cpu_now - current_.begin_cpu_usec_ + 999) / 1000);
    }
    agg.mem_baseline_bytes_ = current_.baseline_;
    agg.mem_avg_bytes_ = current_.count_ > 0 ? current_.sum_ / current_.count_ : 0;
    agg.mem_peak_bytes_ = current_.peak_;
    agg.sample_count_ = current_.count_;
    finished_[task_id] = agg;
    current_ = Window{};
}

bool TaskResourceTracker::take_agg(uint64_t task_id, TaskResourceAgg& out) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = finished_.find(task_id);
    if (it == finished_.end()) return false;
    out = it->second;
    finished_.erase(it);
    return true;
}

}  // namespace fly
