#include <gtest/gtest.h>
#include <agent/cpp/run_metrics.h>
#include <core/cpp/system_info.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <common/testing/cpp/test_helpers.h>

namespace {

// 与 master_agent_test 同款临时目录（自清理）。
class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        path_ = fly::test::qa_tmp_dir("fly_metrics_test_" + tag);
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::filesystem::remove_all(path_); }
    const std::string& path() const { return path_; }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
private:
    std::string path_;
};

void write_file(const std::string& path, size_t bytes) {
    std::ofstream ofs(path, std::ios::binary);
    std::string chunk(bytes, 'x');
    ofs << chunk;
}

std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    std::string content((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    return content;
}

constexpr uint64_t MB = 1024ull * 1024ull;

// 构造一条快照样本。
RunMetricsCollector::Sample S(int64_t rel_ms, uint64_t total_mb) {
    return RunMetricsCollector::Sample{rel_ms, total_mb * MB};
}

std::string expect_avg_peak(const std::string& avg, const std::string& peak) {
    return fmt::format("total_avg={:>8}  total_peak={:>8}", avg, peak);
}

}  // namespace

// ---- 纯函数层：render_runtime_summary / render_db_summary ----

TEST(RunMetricsRenderTest, TenPhasesAveragesAndPeaks) {
    RunMetricsCollector::SummaryInput in;
    in.duration_ms_ = 10000;
    in.tick_seconds_ = 10;
    // 10 个阶段，每阶段 2 条样本：phase i 的 avg=(200i+105)MB peak=(200i+110)MB。
    for (int i = 0; i < 10; ++i) {
        in.samples_.push_back(S(i * 1000 + 100, 100 + 200 * i));
        in.samples_.push_back(S(i * 1000 + 900, 110 + 200 * i));
    }
    std::string out = RunMetricsCollector::render_runtime_summary(in);

    EXPECT_NE(out.find("duration: 10.0s"), std::string::npos);
    // phase 1（i=0）：avg=105MB peak=110MB。
    EXPECT_NE(out.find(expect_avg_peak("105MB", "110MB")), std::string::npos);
    // phase 5（i=4）：avg=905MB peak=910MB。
    EXPECT_NE(out.find(expect_avg_peak("905MB", "910MB")), std::string::npos);
    // phase 10（i=9）：avg=1905MB peak=1910MB。
    EXPECT_NE(out.find(expect_avg_peak("1905MB", "1910MB")), std::string::npos);
    // 尾样本 t=9900 落在最后阶段闭区间 [9000,10000]。
    EXPECT_NE(out.find("phase 10"), std::string::npos);
}

TEST(RunMetricsRenderTest, ShortRunFallsBackToSinglePhase) {
    RunMetricsCollector::SummaryInput in;
    in.duration_ms_ = 5000;
    in.tick_seconds_ = 10;
    for (int i = 0; i < 5; ++i) in.samples_.push_back(S(i * 1000, 100 + i));
    std::string out = RunMetricsCollector::render_runtime_summary(in);

    EXPECT_NE(out.find("1 (fewer than 10 samples)"), std::string::npos);
    EXPECT_EQ(out.find("phase 02"), std::string::npos);
    // 单阶段含全部样本：avg=(100+101+102+103+104)/5=102MB，peak=104MB。
    EXPECT_NE(out.find(expect_avg_peak("102MB", "104MB")), std::string::npos);
}

TEST(RunMetricsRenderTest, DbWindowUsesOnlySamplesInsideWindow) {
    RunMetricsCollector::SummaryInput in;
    in.duration_ms_ = 10000;
    in.tick_seconds_ = 10;
    // 窗口 [2000,5000] 内样本：300/400；窗口外样本统一 1000MB（混入即破坏
    // 期望值）。凑足 10 条避免触发单阶段退化路径（该路径另有专测）。
    in.samples_.push_back(S(100, 1000));
    in.samples_.push_back(S(3000, 300));
    in.samples_.push_back(S(4000, 400));
    for (int64_t t : {1100, 1500, 6000, 6500, 7000, 8500, 9500}) {
        in.samples_.push_back(S(t, 1000));
    }

    RunMetricsCollector::DbView db;
    db.db_path_ = "run/A";
    db.first_seen_ms_ = 2000;
    db.end_ms_ = 5000;
    db.frozen_ = true;
    db.disk_bytes_ = 120 * MB;
    in.dbs_.push_back(db);

    std::string out = RunMetricsCollector::render_db_summary(in);
    // 窗口内：avg=350MB peak=400MB；窗口外（300/400/800/1000…）不得混入。
    EXPECT_NE(out.find(expect_avg_peak("350MB", "400MB")), std::string::npos);
    EXPECT_EQ(out.find(expect_avg_peak("400MB", "800MB")), std::string::npos);
    EXPECT_EQ(out.find(expect_avg_peak("266MB", "400MB")), std::string::npos);
    EXPECT_NE(out.find("db=run/A: disk=  120MB"), std::string::npos);
    EXPECT_NE(out.find("frozen"), std::string::npos);
}

TEST(RunMetricsRenderTest, ActiveDbLabelAndMissingValuesShownAsNA) {
    RunMetricsCollector::SummaryInput in;
    in.duration_ms_ = 1000;
    in.tick_seconds_ = 10;

    RunMetricsCollector::DbView db;
    db.db_path_ = "run/B";
    db.first_seen_ms_ = 0;
    db.end_ms_ = 1000;
    db.frozen_ = false;      // 退出时未 freeze
    db.disk_bytes_ = -1;     // du 失败/未统计
    in.dbs_.push_back(db);

    std::string out = RunMetricsCollector::render_db_summary(in);
    EXPECT_NE(out.find("active-at-exit"), std::string::npos);
    EXPECT_NE(out.find("disk=    n/a"), std::string::npos);
    // 无任何样本 → mem 也是 n/a。
    EXPECT_NE(out.find("mem total_avg=     n/a"), std::string::npos);
}

TEST(RunMetricsRenderTest, EmptyDbSectionRendersNone) {
    RunMetricsCollector::SummaryInput in;
    std::string out = RunMetricsCollector::render_db_summary(in);
    EXPECT_NE(out.find("(none)"), std::string::npos);
}

// ---- 采集层 ----

// 合成语义：最近邻合并 / 未上线不计 / 判死不计 / 复活重计。
// 顺序推进四个状态，逐步断言最新骨架的集群 total。
TEST(RunMetricsCollectorTest, SynthGroupedSamplesNearestNeighborLifecycle) {
    RunMetricsCollector c;
    c.start(0);
    // 第二条骨架（start 已有首 tick）；样本时刻均早于此骨架 → 最近邻计入。
    const uint64_t m1 = c.tick_once_for_testing();
    const uint64_t now = RunMetricsCollector::epoch_ms_now();
    const uint64_t MB = 1024ull * 1024ull;

    // 1) 成组样本（模拟积压补发，3 条历史样本）全部落入该 worker 序列；
    //    合成取 ≤ 骨架 epoch 的最后一条（最近邻=120MB）。
    c.on_worker_samples(1, {now - 30000, now - 20000, now - 10000},
                        {100 * MB, 110 * MB, 120 * MB});
    EXPECT_EQ(c.worker_sample_count_for_testing(1), 3u);
    auto series = c.synth_for_testing();
    ASSERT_GE(series.size(), 2u);
    EXPECT_EQ(series.back().total_bytes_, m1 + 120 * MB);

    // 2) 未上线：样本时刻晚于全部骨架（首样本前语义）→ 不计入。
    c.on_worker_samples(2, {now + 100000}, {200 * MB});
    series = c.synth_for_testing();
    EXPECT_EQ(series.back().total_bytes_, m1 + 120 * MB);

    // 3) 判死：dead epoch = now（晚于 worker1 全部样本）→ 不计入。
    c.on_worker_dead(1);
    series = c.synth_for_testing();
    EXPECT_EQ(series.back().total_bytes_, m1);

    // 4) 复活：epoch > dead 的新样本 + 不早于它的骨架 → 重新计入。
    //    sleep 保证毫秒时间戳严格推进（同毫秒内 dead == 样本 epoch 会误判仍死）。
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const uint64_t revive_epoch = RunMetricsCollector::epoch_ms_now();
    const uint64_t m2 = c.tick_once_for_testing();  // 骨架 epoch ≥ revive_epoch
    c.on_worker_samples(1, {revive_epoch}, {150 * MB});
    series = c.synth_for_testing();
    EXPECT_EQ(series.back().total_bytes_, m2 + 150 * MB);
    c.stop();
}

TEST(RunMetricsCollectorTest, DbDiskMeasuredAtFreeze) {
    TempDir dir("freeze");
    write_file(dir.path() + "/data_0.dat", 3 * MB);

    RunMetricsCollector c;
    c.start(0);
    c.record_db_created(dir.path(), "");
    EXPECT_EQ(c.db_disk_for_testing(dir.path()), -1);  // freeze 前未统计

    c.record_db_frozen(dir.path());
    EXPECT_TRUE(c.db_frozen_for_testing(dir.path()));
    // du 结果 >= 已写文件大小（含目录开销）。
    EXPECT_GE(c.db_disk_for_testing(dir.path()), static_cast<int64_t>(3 * MB));
    c.stop();
}

TEST(RunMetricsCollectorTest, MergeInvalidatesDiskButKeepsFrozen) {
    TempDir dir("merge");
    write_file(dir.path() + "/data_0.dat", 3 * MB);

    RunMetricsCollector c;
    c.start(0);
    c.record_db_created(dir.path(), "");
    c.record_db_frozen(dir.path());
    ASSERT_GE(c.db_disk_for_testing(dir.path()), static_cast<int64_t>(3 * MB));

    // merge set_paths：路径变更作废统计值；frozen 状态保留（frozen_dbs_ 防重 freeze）。
    c.record_db_paths_changed(dir.path(), dir.path() + "/merged_data");
    EXPECT_EQ(c.db_disk_for_testing(dir.path()), -1);
    EXPECT_TRUE(c.db_frozen_for_testing(dir.path()));
    c.stop();
}

TEST(RunMetricsCollectorTest, WriteSummaryFilesCoversActiveDb) {
    TempDir dir1("active");
    TempDir dir2("frozen");
    TempDir outdir("out");
    write_file(dir1.path() + "/data_0.dat", 2 * MB);
    write_file(dir2.path() + "/data_0.dat", 2 * MB);

    RunMetricsCollector c;
    c.start(0);
    c.on_worker_samples(1, {RunMetricsCollector::epoch_ms_now() - 1000}, {100 * MB});
    c.record_db_created(dir1.path(), "");   // 不 freeze（active-at-exit）
    c.record_db_created(dir2.path(), "");
    c.record_db_frozen(dir2.path());        // freeze 时已统计 disk

    c.tick_once_for_testing();
    c.stop();
    auto [rt_path, db_sum_path] = c.write_summary_files(outdir.path(), 10);

    EXPECT_TRUE(std::filesystem::exists(rt_path));
    EXPECT_TRUE(std::filesystem::exists(db_sum_path));
    EXPECT_NE(rt_path.find("runtime.summary"), std::string::npos);
    EXPECT_NE(db_sum_path.find("db.summary"), std::string::npos);
    EXPECT_GT(c.duration_seconds(), 0.0);

    std::string rt = read_file(rt_path);
    EXPECT_NE(rt.find("Fly Run Summary (runtime)"), std::string::npos);
    EXPECT_NE(rt.find("workers seen: 1"), std::string::npos);

    // active db 的 disk 由退出补测路径填充：db=dir1 行不得出现 n/a。
    std::string dbs = read_file(db_sum_path);
    EXPECT_NE(dbs.find("Fly Run Summary (databases)"), std::string::npos);
    EXPECT_NE(dbs.find("active-at-exit"), std::string::npos);
    size_t pos1 = dbs.find("db=" + dir1.path());
    ASSERT_NE(pos1, std::string::npos);
    size_t line_end = dbs.find('\n', pos1);
    std::string line1 = dbs.substr(pos1, line_end - pos1);
    EXPECT_EQ(line1.find("n/a"), std::string::npos);
}

TEST(RunMetricsCollectorTest, DbCreatedBeforeStartIsIgnored) {
    RunMetricsCollector c;
    // 未 start（时间轴未设）时挂钩为 no-op（master 尚未初始化完成的安全兜底）。
    c.record_db_created("db/before_start", "");
    EXPECT_FALSE(c.started_for_testing());
    c.start(0);
    EXPECT_EQ(c.sample_count_for_testing(), 1u);  // start 立即首 tick
    c.stop();
}

TEST(RunMetricsDuTest, DuBytesOnRealAndMissingDir) {
    TempDir dir("du");
    write_file(dir.path() + "/data_0.dat", 2 * MB);
    auto got = RunMetricsCollector::du_bytes(dir.path());
    ASSERT_TRUE(got.has_value());
    EXPECT_GE(*got, 2 * MB);

    EXPECT_FALSE(RunMetricsCollector::du_bytes("/nonexistent_fly_metrics_dir").has_value());
    EXPECT_FALSE(RunMetricsCollector::du_bytes("").has_value());
}
