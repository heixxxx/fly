#include "config.h"
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

CMSharedPtr<Config> Config::instance() {
    static CMSharedPtr<Config> inst = CMMakeShared<Config>();
    return inst;
}

Config::Config() {
    int_values_ = INT_DEFAULTS;
    str_values_ = STR_DEFAULTS;
}

void Config::set_int(const CMString& key, int64_t value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    int_values_[key] = value;
}

void Config::set_str(const CMString& key, const CMString& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (workers_launched_) {
        throw std::runtime_error("Config must be set before workers are launched");
    }
    str_values_[key] = value;
}

int64_t Config::get_int(const CMString& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = int_values_.find(key);
    auto default_it = INT_DEFAULTS.find(key);
    if (it != int_values_.end()) return it->second;
    if (default_it != INT_DEFAULTS.end()) return default_it->second;
    fprintf(stderr, "[ERR] Config::get_int: unknown key '%s'\n", key.c_str());
    return INVALID_INT;
}

CMString Config::get_str(const CMString& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = str_values_.find(key);
    if (it != str_values_.end()) return it->second;
    auto default_it = STR_DEFAULTS.find(key);
    if (default_it != STR_DEFAULTS.end()) return default_it->second;
    return "";
}

void Config::mark_workers_launched() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    workers_launched_ = true;
}

bool Config::is_workers_launched() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return workers_launched_;
}

void Config::reset() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    int_values_ = INT_DEFAULTS;
    str_values_ = STR_DEFAULTS;
    workers_launched_ = false;
}

void Config::save_to_file(const CMString& path) const {
    std::vector<std::pair<CMString, int64_t>> ints;
    std::vector<std::pair<CMString, CMString>> strs;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        for (const auto& [k, v] : int_values_) ints.emplace_back(k, v);
        for (const auto& [k, v] : str_values_) strs.emplace_back(k, v);
    }
    // 文件 IO 在锁外执行，避免阻塞并发 get_*（低频操作，快照语义可接受）
    std::ofstream ofs(path.c_str(), std::ios::trunc);
    for (const auto& [k, v] : ints) ofs << "i " << k << " " << v << "\n";
    for (const auto& [k, v] : strs) ofs << "s " << k << " " << v << "\n";
}

void Config::load_from_file(const CMString& path) {
    std::ifstream ifs(path.c_str());
    if (!ifs.is_open()) return;
    std::vector<std::pair<CMString, int64_t>> ints;
    std::vector<std::pair<CMString, CMString>> strs;
    CMString line;
    while (std::getline(ifs, line)) {
        if (line.size() < 3) continue;
        char type = line[0];
        CMString rest = line.substr(2);
        auto sp = rest.find(' ');
        if (sp == CMString::npos) continue;
        CMString key = rest.substr(0, sp);
        CMString val = rest.substr(sp + 1);
        if (type == 'i') {
            try { ints.emplace_back(key, std::stoll(val)); } catch (...) {}
        } else if (type == 's') {
            strs.emplace_back(key, val);
        }
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& [k, v] : ints) int_values_[k] = v;
    for (auto& [k, v] : strs) str_values_[k] = v;
}

const CMUnorderedMap<CMString, int64_t> Config::INT_DEFAULTS = {
    {"heartbeat_timeout", 120},
    {"heartbeat_interval", 5},
    {"backup_threshold", 100},
    {"auto_backup_enabled", 0},       // 0=disabled, 1=enabled
    {"backup_replicas", 2},           // target number of backup copies (including original)
    {"backup_decay_interval", 300},   // decay check interval in seconds, 0=no decay
    {"backup_decay_factor", 50},      // decay factor percentage (read_count *= factor/100)
    // ── auto-backup 双层重设计（worker suggest + master EWMA 聚合）──
    // worker 侧：TIER2 读累积达阈值 + cooldown 过 → suggest → reset（worker 不时间衰减）。
    {"worker_suggest_bytes_threshold", 1073741824},   // 1GB：worker 累积传输字节达此值触发 suggest
    {"worker_suggest_count_threshold", 100},          // worker 累积读次数达此值触发 suggest
    {"worker_suggest_cooldown", 60},                  // worker 两次 suggest 最小间隔（秒）
    // master 侧：EWMA 聚合 worker suggest，score = cumulative/replicas 判定 backup（不 reset）。
    {"master_ewma_decay_per_sec", 1},                 // master EWMA 每秒衰减百分比（1 = 1%/s）
    {"backup_bytes_threshold", 10737418240},          // 每副本 10GB：score_bytes 超此值触发 backup
    {"backup_count_threshold", 1000},                 // 每副本 1000 次：score_count 超此值触发 backup
    {"max_backup_replicas", 3},                       // 正常副本上限（含原始）
    {"backup_large_object_threshold", 1073741824},    // 1GB+ 视为大文件（可触发例外突破上限）
    {"backup_high_score_threshold", 107374182400},    // 100GB：大文件 score_bytes 超此值触发例外
    {"backup_extra_slots", 2},                        // 例外情况下在 max_backup_replicas 之外额外副本数
    {"aggregation_threshold", 1048576},
    {"large_file_threshold_kb", 65536},  // 64MB in KB (user-configurable)
    {"block_size", 134217728},
    {"track_writes", 0},
    {"data_server_threads", 4},
    {"compression_level", 0},
    {"serialize_chunk_size", 4194304},
    {"compression_threshold", 4096},  // skip compression for payloads <= this size
    {"dependency_update_mode", 0},
    {"locality_scheduling_enabled", 1},  // data locality 调度开关：1=开启(默认), 0=关闭
    {"fail_unscheduleable_tasks", 1},
    {"read_cache_size", 1073741824},
    {"temp_store_size", 2147483648},
    {"data_client_pool_size", 4},
    {"net_probe_enabled", 1},
    {"handler_lanes", 4},              // 消息 handler 并行 lane 数（同连接串行/跨连接并行）；0=全部内联（legacy 单线程）          // 网络感知远程读优先级：1=开启(默认), 0=关闭(排序降级 no-op)
    {"net_probe_interval_ms", 30000},  // 主动带宽探测周期(ms)
    {"net_probe_payload_kb", 256},     // 探测 payload 大小(KB)
    {"net_probe_timeout_ms", 3000},    // 单次探测超时(ms)
    {"solver_openmp_threads", 0},      // Eigen LDLT 分解并行线程数：0=单线程默认，>0=OpenMP多线程
    // worker 生命周期（用户确认语义）：
    // - 首次注册：master 占位符不等待不假设超时（0=默认无限；>0 时超时清理占位符
    //   并作为 wait_workers_registered 默认超时）；worker 首连重试窗口同此键（两侧一致）。
    {"worker_register_timeout", 0},
    // - 断连重连：worker 指数退避重连的宽限窗口，master 对断连 worker 的判死宽限
    //   与之对等（宽限内 task 存活、不重调度；超时判死+快速失败）。0=不重连不宽限
    //   （旧的"断连即死"逃生口）。
    {"worker_reconnect_timeout", 120},
    // - connect 重试首次间隔(ms)，指数 ×2 递增（单次上限 10s 硬编码），首连与重连共用。
    {"worker_connect_retry_initial_ms", 500},
    // Logger 自动 flush（DEBUG/INFO；WARN/ERROR 始终立即 flush）：
    // 累计写入字节数达到阈值，或距上次 flush 超过时间间隔（写时惰性判定，
    // 无后台线程），避免日志文件更新延迟过长。
    {"log_flush_threshold_bytes", 65536},
    {"log_flush_interval_ms", 1000},
};

const CMUnorderedMap<CMString, CMString> Config::STR_DEFAULTS = {
    {"transport_type", "tcp"},
    {"compression_type", "lz4"},
    {"log_dir", "fly_log"},
};
