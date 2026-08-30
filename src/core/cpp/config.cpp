#include "config.h"
#include <cstdio>
#include <unistd.h>
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

bool Config::set_int(const CMString& key, int64_t value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (workers_launched_) {
        fprintf(stderr, "[ERR] Config::set_int rejected after workers launched: key '%s'\n",
                key.c_str());
        return false;
    }
    int_values_[key] = value;
    return true;
}

bool Config::set_str(const CMString& key, const CMString& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (workers_launched_) {
        fprintf(stderr, "[ERR] Config::set_str rejected after workers launched: key '%s'\n",
                key.c_str());
        return false;
    }
    str_values_[key] = value;
    return true;
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
        for (const auto& [k, v] : strs) strs.emplace_back(k, v);
    }
    // 文件 IO 在锁外执行，避免阻塞并发 get_*（低频操作，快照语义可接受）。
    // 原子写（tmp + rename）：.fly_config 是 worker 引导的唯一寻址来源，
    // 重写窗口内（P2 幂等重写 / 并发读）不得让读者见到半文件。
    std::string tmp = path.c_str() + std::string(".tmp.");
    tmp += std::to_string(::getpid());
    {
        std::ofstream ofs(tmp, std::ios::trunc);
        for (const auto& [k, v] : ints) ofs << "i " << k << " " << v << "\n";
        for (const auto& [k, v] : strs) ofs << "s " << k << " " << v << "\n";
        ofs.flush();
        if (!ofs.good()) {
            ofs.close();
            ::unlink(tmp.c_str());
            return;
        }
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
    }
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
    {"auto_backup_enabled", 0},       // 0=disabled, 1=enabled
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
    // L3 流式读（§8.1）：接收线程有界队列上限（片数，默认 16 片 ≈ 64MB 压缩态）。
    //（streaming_read_threshold 已删除——2026-08-30 恒流式裁定：常规读统一
    // 流式传输，无阈值分流与关闭逃生口。）
    {"stream_buffer_chunks", 16},
    // L1 流式写（§9.1）：>0 启用——pickle.dump 流入 → 压缩块直写增量 record
    //（内存 R+常数；写前不知对象大小，开关启用即统一走流式）。0 = 关闭逃生口。
    {"streaming_write_threshold", 1},
    // L2 分片传输阈值（chunked-transfer-design §4.5）：对象 record 超过此字节数
    // 时 DataServer 走分片路径（META + 4MB CHUNK 流 + DIGEST），否则整帧快路径。
    {"chunked_transfer_threshold", 4194304},
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
    // - 正常收尾 stop() 的 drain 等待上限(s)：等 RUNNING task 全部完成，超时
    //   转 fast 路径（fail 善后留痕 + StopNow 收尾）。兜底"worker 活着但
    //   complete 丢失"的僵死路径（4 实例压测实测卡死）。0=无限等待（逃生口）。
    {"drain_timeout_seconds", 600},
    // ── 运行时统计（机器信息日志 + RunSummary 集群内存）──
    // 机器信息 INFO 日志间隔(s)：每进程（master+worker）定期打印 proc_rss/
    // host free/total/cpu%/loadavg。0=关闭。
    {"machine_info_interval_seconds", 10},
    // master 集群内存快照间隔(s)：RunMetricsCollector 每 tick 记一次
    // total = master RSS + Σ活跃 worker 最新上报 RSS，退出时汇总为
    // Run Summary 的分阶段 total_avg/total_peak 与按 db 窗口统计。
    {"metrics_tick_seconds", 10},
    // ── cluster monitor（负载采样 + monitor.db 落盘 + Web GUI）──
    // 采样间隔(ms)：worker/master 每 MonitorSample 一条（RSS/双CPU%/host 内存/
    // 网络 IO/loadavg）。0=关闭采样上报。
    {"monitor_sample_interval_ms", 1000},
    // 最小采样间距(ms)：事件驱动采样（assign/执行起止/IO/断连等 cluster 事件
    // 时刻的快照）与执行窗口内加密采样的统一节流下限——事件密集期样本密度
    // 封顶 1000/此值，风暴不刷爆 DB；读写快的 IO 天然被间距挡掉。
    {"monitor_exec_sample_interval_ms", 200},
    // 成组上报间隔(ms)：worker 攒批经 MONITOR_SAMPLE 发 master（组内样本
    // 时间升序，失败/断连缓冲不丢，下次成组补发）。
    {"monitor_report_interval_ms", 10000},
    // master 单写 {log_dir}/monitor.db（1=开启，0=本 run 无持久化监控）。
    {"monitor_db_enabled", 1},
    // - connect 重试首次间隔(ms)，指数 ×2 递增（单次上限 10s 硬编码），首连与重连共用。
    {"worker_connect_retry_initial_ms", 500},
    // - 注册 ack 丢失兜底（P3-23）：注册守望的超时退避初值(ms)，指数 ×2
    //   （单次上限 30s 硬编码）。覆盖 master 活着但注册/ack 被应用层吞掉的
    //   场景；连接级丢失由 on_disconnect 事件驱动恢复（无超时参与）。
    {"worker_register_ack_retry_initial_ms", 500},
    // Logger 自动 flush（DEBUG/INFO；WARN/ERROR 始终立即 flush）：
    // 累计写入字节数达到阈值，或距上次 flush 超过时间间隔（写时惰性判定，
    // 无后台线程），避免日志文件更新延迟过长。
    {"log_flush_threshold_bytes", 65536},
    {"log_flush_interval_ms", 1000},
    // 存储接管（判死后同 host storage_only 只读加载死 worker 的 idx 接管读
    // 服务，复用 IdxLoad 链路）。默认关（保守）；接管在途时全灭 fail 延迟至
    // fail_timeout，超时幂等重判兜底。max_writers=每 storage 接管 writer 数
    // 上限（0=不限），防同 host 多 worker 连挂涌向单一 storage。
    {"storage_takeover_enabled", 0},
    {"storage_takeover_fail_timeout", 60},
    {"storage_takeover_max_writers", 64},
    // 自动补齐存储节点（master 周期检测「有活 worker 但无 storage_only」的
    // host，经该 host 的活 worker 本地 spawn storage worker）。默认关（opt-in）
    //——LSF/bsub 等调度器环境下 spawn 侵占作业资源配额，须在允许的环境显式
    // 开启；外部 launcher（expect_workers 占位）唤起的 storage 同样被检测视
    // 为已覆盖。检测周期由 heartbeat 循环节流（5s 一轮）。
    {"auto_storage_nodes_enabled", 0},
    {"auto_storage_check_interval", 30},
    // master 监听端口（master 侧落盘前的占位 0；.fly_config 内为定稿值）。
    {"master_port", 0},
};

const CMUnorderedMap<CMString, CMString> Config::STR_DEFAULTS = {
    {"transport_type", "tcp"},
    {"compression_type", "lz4"},
    {"log_dir", "fly_log"},
    // master 寻址（worker 引导）：master 侧在 .fly_config 首次落盘前写入
    // advertise 地址与定稿端口，worker 进程 main 加载 config 后经 ProcessInfo
    // 兑现——local/ssh/bsub 三类启动统一从文件取址，CLI --master-host/port
    // 仅作调试覆盖口。空值 = 未经 master 落盘（worker 直连 127.0.0.1 兜底）。
    {"master_host", ""},
    {"master_advertise_host", ""},
};
