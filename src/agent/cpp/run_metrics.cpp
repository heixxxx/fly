#include <agent/cpp/run_metrics.h>

#include <core/cpp/system_info.h>
#include <log/cpp/logger.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

#include <fmt/format.h>

RunMetricsCollector::~RunMetricsCollector() {
    stop();
}

// ---- 纯函数汇总层 ----

namespace {

// 字节 → "480MB"（MB 粒度取整；集群总量口径无需更细）。
std::string mb_str(uint64_t bytes) {
    return fmt::format("{}MB", bytes / (1024ull * 1024ull));
}

// 字节自适应（db 磁盘用量专用）："4KB"/"480MB"/"1.5GB"——KB 级小 db
// 不应被 MB 取整吞成 0MB。
std::string size_str(uint64_t bytes) {
    constexpr uint64_t kb = 1024ull;
    constexpr uint64_t mb = 1024ull * 1024ull;
    constexpr uint64_t gb = 1024ull * 1024ull * 1024ull;
    if (bytes < mb) return fmt::format("{}KB", bytes / kb);
    if (bytes < gb) return fmt::format("{}MB", bytes / mb);
    return fmt::format("{:.1f}GB", static_cast<double>(bytes) / gb);
}

// [lo, hi]（rel_ms，闭区间）窗口内样本的 avg/peak。n=0 时返回 false。
bool window_stats(const CMVector<RunMetricsCollector::Sample>& samples,
                  int64_t lo, int64_t hi, uint64_t& avg, uint64_t& peak) {
    uint64_t sum = 0;
    peak = 0;
    size_t n = 0;
    for (const auto& s : samples) {
        if (s.rel_ms_ < lo || s.rel_ms_ > hi) continue;
        sum += s.total_bytes_;
        if (s.total_bytes_ > peak) peak = s.total_bytes_;
        ++n;
    }
    if (n == 0) return false;
    avg = sum / n;
    return true;
}

}  // namespace

CMString RunMetricsCollector::render_runtime_summary(const SummaryInput& in) {
    const int64_t dur = in.duration_ms_ > 0 ? in.duration_ms_ : 0;
    std::string out;

    out += "========== Fly Run Summary (runtime) ==========\n";
    out += fmt::format("duration: {:.1f}s  (dbs: {}, workers seen: {})\n",
                       dur / 1000.0, in.dbs_.size(), in.workers_seen_);

    // 分阶段集群内存（总时长 10 等份；样本不足 10 条退化为单阶段）。
    const bool single_phase = in.samples_.size() < 10;
    const int n_phases = single_phase ? 1 : 10;
    out += fmt::format(
        "cluster memory (physical RSS, master+workers) by phase ({} phases, sampled every {}s):\n",
        single_phase ? "1 (fewer than 10 samples)" : "10", in.tick_seconds_);

    for (int i = 0; i < n_phases; ++i) {
        int64_t lo, hi;
        if (single_phase) {
            lo = 0;
            hi = dur;
        } else {
            lo = dur * i / 10;
            hi = (i == n_phases - 1) ? dur : dur * (i + 1) / 10 - 1;
        }
        uint64_t avg = 0, peak = 0;
        CMString line;
        if (window_stats(in.samples_, lo, hi, avg, peak)) {
            line = fmt::format("  phase {:02d} [{:7.1f}s, {:7.1f}s]: total_avg={:>8}  total_peak={:>8}\n",
                               i + 1, lo / 1000.0, hi / 1000.0,
                               mb_str(avg), mb_str(peak));
        } else {
            line = fmt::format("  phase {:02d} [{:7.1f}s, {:7.1f}s]: total_avg={:>8}  total_peak={:>8}\n",
                               i + 1, lo / 1000.0, hi / 1000.0, "n/a", "n/a");
        }
        out += line;
    }

    return out;
}

CMString RunMetricsCollector::render_db_summary(const SummaryInput& in) {
    std::string out;
    out += "========== Fly Run Summary (databases) ==========\n";
    out += "per-database:\n";
    if (in.dbs_.empty()) {
        out += "  (none)\n";
    }
    for (const auto& db : in.dbs_) {
        uint64_t avg = 0, peak = 0;
        bool has_mem = window_stats(in.samples_, db.first_seen_ms_, db.end_ms_, avg, peak);
        std::string disk = db.disk_bytes_ >= 0 ? size_str(static_cast<uint64_t>(db.disk_bytes_)) : "n/a";
        std::string mem_avg = has_mem ? mb_str(avg) : "n/a";
        std::string mem_peak = has_mem ? mb_str(peak) : "n/a";
        out += fmt::format("  db={}: disk={:>7}  duration={:7.1f}s  {:<16} mem total_avg={:>8}  total_peak={:>8}\n",
                           db.db_path_, disk,
                           (db.end_ms_ - db.first_seen_ms_) / 1000.0,
                           db.frozen_ ? "frozen" : "active-at-exit",
                           mem_avg, mem_peak);
    }
    return out;
}

// ---- 采集层 ----

uint64_t RunMetricsCollector::epoch_ms_now() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

void RunMetricsCollector::start(int64_t tick_seconds) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        start_t_ = std::chrono::steady_clock::now();
        start_epoch_ms_ = static_cast<int64_t>(epoch_ms_now());
        started_.store(true);
        tick_seconds_ = tick_seconds > 0 ? tick_seconds : 10;
    }
    // 立即首 tick（短 run 也有样本）。
    tick_once();
    if (tick_seconds > 0) {
        tick_running_.store(true);
        tick_thread_ = std::thread([this] { tick_loop(); });
    }
}

void RunMetricsCollector::stop() {
    tick_running_.store(false);
    {
        // 持锁 notify（规范）：tick waiter 的条件检查在锁内，防 lost wakeup。
        std::lock_guard<std::mutex> lk(tick_mutex_);
        tick_cv_.notify_all();
    }
    if (tick_thread_.joinable()) {
        tick_thread_.join();
    }
    std::lock_guard<std::mutex> lk(mutex_);
    if (duration_ms_ < 0 && started_.load()) {
        duration_ms_ = now_rel_ms_locked();
    }
}

void RunMetricsCollector::tick_loop() {
    while (tick_running_.load()) {
        {
            std::unique_lock<std::mutex> lk(tick_mutex_);
            tick_cv_.wait_for(lk, std::chrono::seconds(tick_seconds_),
                              [this] { return !tick_running_.load(); });
        }
        if (!tick_running_.load()) break;
        tick_once();
    }
}

void RunMetricsCollector::tick_once() {
    // 锁外读 master 自身 RSS（/proc 微秒级读，但不占 collector 锁）。
    const uint64_t master_rss = fly::SystemInfo::process_rss_bytes();
    const int64_t epoch_ms = static_cast<int64_t>(epoch_ms_now());
    std::lock_guard<std::mutex> lk(mutex_);
    if (!started_.load()) return;
    const int64_t rel = now_rel_ms_locked();
    master_samples_.push_back(MasterTick{rel, epoch_ms, master_rss});
}

int64_t RunMetricsCollector::now_rel_ms_locked() const {
    // 微秒向上取整到毫秒：start/stop 同毫秒的极短窗口也能得到 ≥1ms 的
    // duration（duration_seconds()==0 的 flaky 根治——测试与 summary 渲染
    // 都依赖 duration > 0）。
    return (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_t_)
                .count() + 999) / 1000;
}

void RunMetricsCollector::on_worker_samples(uint64_t worker_id,
                                            const CMVector<uint64_t>& epoch_ms,
                                            const CMVector<uint64_t>& rss_bytes) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto& samples = worker_samples_[worker_id];
    for (size_t i = 0; i < epoch_ms.size() && i < rss_bytes.size(); ++i) {
        if (rss_bytes[i] == 0) continue;
        samples.emplace_back(static_cast<int64_t>(epoch_ms[i]), rss_bytes[i]);
    }
    workers_seen_.insert(worker_id);
}

void RunMetricsCollector::on_worker_dead(uint64_t worker_id) {
    std::lock_guard<std::mutex> lk(mutex_);
    worker_dead_epoch_[worker_id] = static_cast<int64_t>(epoch_ms_now());
}

void RunMetricsCollector::record_db_created(const CMString& db_path, const CMString& data_dir) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!started_.load() || dbs_.count(db_path)) return;  // 首见语义：不覆盖
    DbStat st;
    st.first_seen_ms_ = now_rel_ms_locked();
    st.data_dir_ = data_dir;
    dbs_[db_path] = st;
}

void RunMetricsCollector::record_db_frozen(const CMString& db_path) {
    CMString data_dir;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!started_.load()) return;
        auto it = dbs_.find(db_path);
        if (it == dbs_.end() || it->second.frozen_ms_ >= 0) return;  // 幂等
        it->second.frozen_ms_ = now_rel_ms_locked();
        data_dir = it->second.data_dir_;
    }
    // 锁外 du：freeze 后无写入，此即终值；ms 级，与 freeze 自身 IO 同量级。
    const int64_t disk = measure_disk(db_path, data_dir);
    if (disk < 0) {
        WARN("RunMetrics: du failed for frozen db {} (will retry at exit)", db_path);
    }
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = dbs_.find(db_path);
    if (it != dbs_.end()) {
        it->second.disk_bytes_ = disk;
    }
}

void RunMetricsCollector::record_db_paths_changed(const CMString& db_path,
                                                  const CMString& new_data_dir) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = dbs_.find(db_path);
    if (it == dbs_.end()) return;
    it->second.disk_bytes_ = -1;  // 路径已变，旧统计作废（退出时补测）
    it->second.data_dir_ = new_data_dir;
}

namespace {

// worker 在 epoch 时刻的最新已知值：≤ epoch 的最后样本（序列按 epoch 升序，
// std::upper_bound 最近邻）。首样本前（未上线）不计；判死后无复活样本
// （最后样本时刻 ≤ dead_epoch）不计。计入时写 out 并返回 true。
bool latest_worker_value(const CMVector<std::pair<int64_t, uint64_t>>& samples,
                         int64_t epoch, int64_t dead_epoch, uint64_t& out) {
    auto it = std::upper_bound(samples.begin(), samples.end(), epoch,
                               [](int64_t e, const std::pair<int64_t, uint64_t>& s) {
                                   return e < s.first;
                               });
    if (it == samples.begin()) return false;  // 首样本前：未上线
    --it;
    if (dead_epoch > 0 && it->first <= dead_epoch) return false;  // 死后无新样本
    out = it->second;
    return true;
}

}  // namespace

// 合成集群 total 序列：每骨架 tick，各 worker 取该 epoch 时刻的最新已知
// 样本（最近邻）。骨架数 × worker 数 × log(样本数)，退出时一次性。
CMVector<RunMetricsCollector::Sample> RunMetricsCollector::synth_cluster_series(
    const CMVector<MasterTick>& ticks,
    const CMUnorderedMap<uint64_t, CMVector<std::pair<int64_t, uint64_t>>>& ws,
    const CMUnorderedMap<uint64_t, int64_t>& wd) {
    CMVector<Sample> out;
    out.reserve(ticks.size());
    for (const auto& tick : ticks) {
        uint64_t total = tick.rss_bytes_;
        for (const auto& [wid, samples] : ws) {
            auto dit = wd.find(wid);
            const int64_t dead = dit == wd.end() ? 0 : dit->second;
            uint64_t v = 0;
            if (latest_worker_value(samples, tick.epoch_ms_, dead, v)) {
                total += v;
            }
        }
        out.push_back(Sample{tick.rel_ms_, total});
    }
    return out;
}

std::pair<CMString, CMString> RunMetricsCollector::write_summary_files(
    const CMString& log_dir, int64_t tick_seconds) {
    SummaryInput in;
    struct DbOut {
        DbView view_;
        CMString data_dir_;
        bool need_disk_;
    };
    CMVector<DbOut> outs;

    {
        std::lock_guard<std::mutex> lk(mutex_);
        in.samples_ = synth_cluster_series(master_samples_, worker_samples_,
                                           worker_dead_epoch_);
        in.workers_seen_ = workers_seen_.size();
        in.duration_ms_ = duration_ms_ >= 0 ? duration_ms_ : now_rel_ms_locked();
        for (const auto& [path, st] : dbs_) {
            DbOut o;
            o.view_.db_path_ = path;
            o.view_.first_seen_ms_ = st.first_seen_ms_;
            o.view_.frozen_ = st.frozen_ms_ >= 0;
            o.view_.end_ms_ = st.frozen_ms_ >= 0 ? st.frozen_ms_ : in.duration_ms_;
            o.view_.disk_bytes_ = st.disk_bytes_;
            o.data_dir_ = st.data_dir_;
            // 补测集合：未 freeze / merge 作废 / freeze 时 du 曾失败。
            o.need_disk_ = st.disk_bytes_ < 0;
            outs.push_back(o);
        }
    }

    // 锁外补测（stop 后调用，tick 线程已停；典型 run 全部 db 已 freeze，零 du）。
    for (auto& o : outs) {
        if (!o.need_disk_) continue;
        const int64_t disk = measure_disk(o.view_.db_path_, o.data_dir_);
        if (disk < 0) {
            WARN("RunMetrics: exit du failed for db {}", o.view_.db_path_);
        }
        o.view_.disk_bytes_ = disk;
    }
    // 补测结果回写 dbs_：monitor 的 stop 收尾（record_db_du）在 summary 之后
    // 读 db_disk_bytes，不回写会拿到补测前的 -1。
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& o : outs) {
            if (!o.need_disk_) continue;
            auto it = dbs_.find(o.view_.db_path_);
            if (it != dbs_.end() && o.view_.disk_bytes_ >= 0) {
                it->second.disk_bytes_ = o.view_.disk_bytes_;
            }
        }
    }
    for (const auto& o : outs) {
        in.dbs_.push_back(o.view_);
    }

    in.tick_seconds_ = tick_seconds > 0 ? tick_seconds : 10;

    // 独立 ofstream 直写（构造即开、析构 flush+close），不经 Logger 通道——
    // 退出期 Logger INFO 有偶发吞行前科（2026-08-19 压测 [SD] 取证）。
    CMString dir = log_dir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    const CMString rt_path = dir + "runtime.summary";
    const CMString db_sum_path = dir + "db.summary";
    {
        std::ofstream rt_ofs(rt_path);
        rt_ofs << render_runtime_summary(in);
    }
    {
        std::ofstream db_ofs(db_sum_path);
        db_ofs << render_db_summary(in);
    }
    return {rt_path, db_sum_path};
}

double RunMetricsCollector::duration_seconds() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return duration_ms_ < 0 ? 0.0 : static_cast<double>(duration_ms_) / 1000.0;
}

int64_t RunMetricsCollector::measure_disk(const CMString& db_path, const CMString& data_dir) {
    auto a = du_bytes(db_path);
    if (!a) return -1;
    uint64_t total = *a;
    if (!data_dir.empty() && data_dir != db_path) {
        auto b = du_bytes(data_dir);
        if (!b) return -1;
        total += *b;
    }
    return static_cast<int64_t>(total);
}

std::optional<uint64_t> RunMetricsCollector::du_bytes(const CMString& dir) {
    if (dir.empty()) return std::nullopt;
    // 单引号转义防 shell 命令注入（db_path 用户可控）。
    std::string safe;
    safe.reserve(dir.size() + 8);
    for (char c : dir) {
        if (c == '\'') {
            safe += "'\\''";
        } else {
            safe += c;
        }
    }
    const std::string cmd = "du -sk '" + safe + "' 2>/dev/null";
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) return std::nullopt;
    char buf[128];
    unsigned long long kb = 0;
    const bool ok = ::fgets(buf, sizeof(buf), p) != nullptr &&
                    ::sscanf(buf, "%llu", &kb) == 1;
    const int rc = ::pclose(p);
    if (!ok || rc != 0 || kb == 0) return std::nullopt;
    return static_cast<uint64_t>(kb) * 1024ull;
}

// ---- 单测驱动 ----

uint64_t RunMetricsCollector::tick_once_for_testing() {
    tick_once();
    std::lock_guard<std::mutex> lk(mutex_);
    return master_samples_.empty() ? 0 : master_samples_.back().rss_bytes_;
}

size_t RunMetricsCollector::sample_count_for_testing() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return master_samples_.size();
}

size_t RunMetricsCollector::worker_sample_count_for_testing(uint64_t worker_id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = worker_samples_.find(worker_id);
    return it == worker_samples_.end() ? 0 : it->second.size();
}

CMVector<RunMetricsCollector::Sample> RunMetricsCollector::synth_for_testing() {
    std::lock_guard<std::mutex> lk(mutex_);
    return synth_cluster_series(master_samples_, worker_samples_, worker_dead_epoch_);
}

int64_t RunMetricsCollector::db_disk_for_testing(const CMString& db_path) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = dbs_.find(db_path);
    return it == dbs_.end() ? -1 : it->second.disk_bytes_;
}

int64_t RunMetricsCollector::db_disk_bytes(const CMString& db_path) const {
    return db_disk_for_testing(db_path);
}

bool RunMetricsCollector::db_frozen_for_testing(const CMString& db_path) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = dbs_.find(db_path);
    return it != dbs_.end() && it->second.frozen_ms_ >= 0;
}
