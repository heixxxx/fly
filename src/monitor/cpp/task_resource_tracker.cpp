#include <monitor/cpp/task_resource_tracker.h>
#include <core/cpp/system_info.h>

#include <unistd.h>

namespace fly {

namespace {
int64_t cpu_jiffies_now() {
    return SystemInfo::process_cpu_jiffies();
}
uint64_t jiffy_delta_to_ms(int64_t dj, long hz) {
    if (dj <= 0 || hz <= 0) return 0;
    return static_cast<uint64_t>(dj * 1000 / hz);
}
}  // namespace

void TaskResourceTracker::begin(uint64_t task_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    current_ = Window{};
    current_.task_id_ = task_id;
    current_.exec_start_ms_ = monitor_epoch_ms_now();
    current_.begin_jiffies_ = cpu_jiffies_now();
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
    const long hz = sysconf(_SC_CLK_TCK);
    agg.cpu_time_ms_ = jiffy_delta_to_ms(cpu_jiffies_now() - current_.begin_jiffies_, hz);
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
