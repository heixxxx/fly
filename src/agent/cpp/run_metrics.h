#pragma once

#include <container/cpp/container_aliases.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

// RunMetricsCollector —— master 进程专属的运行时指标采集与汇总（RunSummary）。
//
// 采集模型（骨架 + 最近邻合成，全部推迟到退出时合成）：
//   · master tick 线程每 metrics_tick_seconds 采自身 RSS，形成骨架序列
//     {steady 相对时刻（渲染时间轴，保单调），epoch 毫秒（对齐域），rss}。
//   · worker 心跳成组上报 RSS 样本（真实采样时刻 = unix epoch 毫秒；发送
//     失败的样本在 worker 侧缓冲不丢，下次成组补发）。
//   · 退出时合成：对每个骨架 tick，total = master RSS + Σ各 worker 在该
//     tick epoch 时刻的最新已知值（≤ 该时刻的最后样本，std::lower_bound
//     最近邻——用户裁定：按 master 自身 tick 间隔把 worker 时间点合并到
//     最近 tick 即可；首样本前不计；判死时刻后无新样本不计，复活样本
//     epoch > dead_epoch 自然重新生效）。
//
// db 生命周期：record_db_created（master 首见）/ record_db_frozen（freeze
// 确认 + 锁外 du -sk 统计 disk 终值——freeze 后 is_frozen 拦截所有写入，
// 结果即终值）/ record_db_paths_changed（merge set_paths 作废已统计 disk，
// 路径已变；frozen_dbs_ 防重复 freeze，该 db 归入退出时补测集合）。
//
// 分层：render_summary 是纯函数（输入合成后的集群 total 序列 + db 窗口，
// 输出文本），单测直接构造数据；采集层（tick/du/合成）只负责产出喂给它
// 的数据。渲染时间轴统一用相对 start 的毫秒（rel_ms）。
class RunMetricsCollector {
public:
    // ---- 汇总视图（纯函数层）----
    struct Sample {
        int64_t rel_ms_ = 0;
        uint64_t total_bytes_ = 0;
    };
    struct DbView {
        CMString db_path_;
        int64_t first_seen_ms_ = 0;
        int64_t end_ms_ = 0;         // freeze 时刻或退出时刻
        bool frozen_ = false;
        int64_t disk_bytes_ = -1;    // -1 = 未统计/失败（显示 n/a）
    };
    struct SummaryInput {
        int64_t duration_ms_ = 0;
        CMVector<Sample> samples_;
        CMVector<DbView> dbs_;
        size_t workers_seen_ = 0;
        int64_t tick_seconds_ = 10;
    };

    // 纯函数：运行时部分（duration + 分阶段集群内存）。总时长 10 等份；
    // 样本 < 10 时退化为单阶段并注明。
    static CMString render_runtime_summary(const SummaryInput& in);
    // 纯函数：按 db 部分（disk / duration / frozen / mem avg/peak）。
    static CMString render_db_summary(const SummaryInput& in);

    // du -sk <dir>：返回该目录磁盘占用（字节）。失败/目录不存在返回 nullopt。
    static std::optional<uint64_t> du_bytes(const CMString& dir);
    // 当前真实时间点（unix epoch 毫秒，system_clock）。
    static uint64_t epoch_ms_now();

    RunMetricsCollector() = default;
    ~RunMetricsCollector();

    RunMetricsCollector(const RunMetricsCollector&) = delete;
    RunMetricsCollector& operator=(const RunMetricsCollector&) = delete;

    // ---- 采集层生命周期 ----
    // 记录 start 时刻并启动 tick 线程；立即首 tick（短 run 也有样本）。
    // tick_seconds <= 0 时仍设定时间轴（供手动驱动/测试），不起线程。
    void start(int64_t tick_seconds);
    // 停 tick 线程并封闭统计窗口（记录 stop 时刻）。幂等，可安全重入。
    void stop();

    // ---- 采集钩子（master 各处调用，快速非阻塞路径）----
    // worker 心跳成组样本（on_heartbeat）：epoch_ms 与 rss 平行数组，
    // 时间升序。组内含发送失败期间积压补发的样本。
    void on_worker_samples(uint64_t worker_id, const CMVector<uint64_t>& epoch_ms,
                           const CMVector<uint64_t>& rss_bytes);
    // master 判死该 worker（合成时其最后样本不再计入，直到复活新样本）。
    void on_worker_dead(uint64_t worker_id);
    // master 首见 db（register_database）。data_dir 非空且 != db_path 时
    // disk 统计含两目录（merge 后 .dat 可能独立于 db 目录）。
    // 首见语义：已登记的 db_path 不覆盖（merge 路径变更走 paths_changed 钩子）。
    void record_db_created(const CMString& db_path, const CMString& data_dir);
    // freeze 确认（frozen_dbs_.insert 成功处）：封闭 db 窗口 + 统计 disk
    // 终值（du 锁外执行，ms 级；失败保留 -1，退出时重试）。幂等。
    void record_db_frozen(const CMString& db_path);
    // merge set_paths：路径变更作废已统计 disk，记录新 data_dir。
    void record_db_paths_changed(const CMString& db_path, const CMString& new_data_dir);

    // 退出时汇总（stop 之后调用）：先合成集群 total 序列（最近邻合并到
    // 骨架），再对未 freeze/disk 作废/du 曾失败的 db 同步补测，最后直写
    // {log_dir}/runtime.summary 与 {log_dir}/db.summary（独立 ofstream 通道，
    // 不经 Logger——退出期 Logger INFO 有偶发吞行前科），返回两文件路径。
    std::pair<CMString, CMString> write_summary_files(const CMString& log_dir,
                                                      int64_t tick_seconds);
    // 总耗时秒（stop 封闭后有效；未 stop 返回 0）。
    double duration_seconds() const;

    // ---- 单测驱动 ----
    // 手动驱动一次骨架采样（不依赖 tick 线程；需已 start）。返回本次 master RSS。
    uint64_t tick_once_for_testing();
    // 骨架样本数。
    size_t sample_count_for_testing() const;
    // 某 worker 收到的样本总条数（成组补发验证）。
    size_t worker_sample_count_for_testing(uint64_t worker_id) const;
    // 按当前数据合成集群 total 序列（build_summary 的合成段，验证最近邻/
    // 判死/复活语义用）。
    CMVector<Sample> synth_for_testing();
    // db 的 disk 统计值（-1 = 未统计/作废/失败）。
    int64_t db_disk_for_testing(const CMString& db_path) const;
    // 同上，正式读数（monitor 的 DBs 页磁盘占用经 master 在 freeze/收尾时
    // 读取落库）。freeze 后为终值；active db 在 stop 补测后有效。
    int64_t db_disk_bytes(const CMString& db_path) const;
    // db 是否已 freeze。
    bool db_frozen_for_testing(const CMString& db_path) const;
    // 时间轴已启动（start 已调用）。
    bool started_for_testing() const { return started_.load(); }

private:
    struct DbStat {
        int64_t first_seen_ms_ = 0;
        int64_t frozen_ms_ = -1;     // -1 = 未 freeze
        int64_t disk_bytes_ = -1;    // -1 = 未统计/作废/失败
        CMString data_dir_;
    };
    // 骨架 tick：steady 相对时刻（渲染）+ epoch 毫秒（与 worker 样本对齐）。
    struct MasterTick {
        int64_t rel_ms_ = 0;
        int64_t epoch_ms_ = 0;
        uint64_t rss_bytes_ = 0;
    };

    int64_t now_rel_ms_locked() const;
    // 采一条骨架样本（锁外读 master RSS，锁内追加）。
    void tick_once();
    void tick_loop();
    // 合成集群 total 序列：每骨架 tick，各 worker 取该 epoch 时刻的最新已知
    // 样本（最近邻；未上线/判死未复活不计）。
    static CMVector<Sample> synth_cluster_series(
        const CMVector<MasterTick>& ticks,
        const CMUnorderedMap<uint64_t, CMVector<std::pair<int64_t, uint64_t>>>& ws,
        const CMUnorderedMap<uint64_t, int64_t>& wd);
    // du(db_path) + du(data_dir)（非空且不同目录）求和；任一失败返回 -1。
    static int64_t measure_disk(const CMString& db_path, const CMString& data_dir);

    mutable std::mutex mutex_;
    CMVector<MasterTick> master_samples_;           // 有序追加（rel/epoch 递增）
    CMUnorderedMap<uint64_t, CMVector<std::pair<int64_t, uint64_t>>> worker_samples_;
    CMUnorderedMap<uint64_t, int64_t> worker_dead_epoch_;
    CMUnorderedSet<uint64_t> workers_seen_;
    CMUnorderedMap<CMString, DbStat> dbs_;
    std::chrono::steady_clock::time_point start_t_{};
    int64_t start_epoch_ms_ = 0;
    std::atomic<bool> started_{false};
    int64_t duration_ms_ = -1;                     // stop 封闭；-1 = 未 stop

    std::atomic<bool> tick_running_{false};
    std::mutex tick_mutex_;
    std::condition_variable tick_cv_;
    std::thread tick_thread_;
    int64_t tick_seconds_ = 10;                    // start 后只读
};
