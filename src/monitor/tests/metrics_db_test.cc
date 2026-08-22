// MetricsDb 单测：建库/schema、task 全量 UPSERT、样本批量与幂等、
// 并发入队、close 后数据一致性（用 sqlite3 C API 直读文件断言）。
#include <gtest/gtest.h>
#include <monitor/cpp/metrics_db.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <sqlite3.h>

namespace {

// 与 run_metrics_test 同款自清理临时目录。
class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = "/tmp/fly_metricsdb_test_" + tag + "_" + std::to_string(ts) + "_" +
                std::to_string(reinterpret_cast<uintptr_t>(this));
        std::filesystem::create_directories(path_);
    }
    ~TempDir() { std::filesystem::remove_all(path_); }
    const std::string& path() const { return path_; }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
private:
    std::string path_;
};

// 打开 DB 文件执行单值查询（返回第一行第一列的 int64）。
int64_t query_int(const std::string& db_path, const std::string& sql) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    sqlite3_stmt* stmt = nullptr;
    int64_t result = -1;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int64(stmt, 0);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

// 打开 DB 文件执行单值文本查询（返回第一行第一列的 string）。
std::string query_text(const std::string& db_path, const std::string& sql) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return "<open-failed>";
    }
    sqlite3_stmt* stmt = nullptr;
    std::string result = "<no-row>";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) result = reinterpret_cast<const char*>(text);
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}

TEST(MetricsDbTest, OpenCreatesSchemaAndCloseLeavesCleanFile) {
    TempDir tmp("open");
    fly::MetricsDb db;
    ASSERT_TRUE(db.open(tmp.path()));
    db.record_run_meta("run_start_ms", "12345");
    db.close();
    EXPECT_FALSE(db.opened());

    // close 后文件存在且 schema 齐全（journal 已随最后一次 COMMIT 清理）。
    const std::string path = tmp.path() + "/monitor.db";
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM meta"), 1);
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM worker_samples"), 0);
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM tasks"), 0);
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM object_io"), 0);
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM events"), 0);
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM workers"), 0);

    // 重复 close 幂等；close 后 record_* 静默丢弃不崩溃。
    db.close();
    db.record_run_meta("k", "v");
}

TEST(MetricsDbTest, OpenIsIdempotent) {
    TempDir tmp("idem");
    fly::MetricsDb db;
    ASSERT_TRUE(db.open(tmp.path()));
    EXPECT_TRUE(db.open(tmp.path()));  // 已打开直接成功
    db.close();
}

TEST(MetricsDbTest, OpenFailsOnMissingDir) {
    fly::MetricsDb db;
    EXPECT_FALSE(db.open("/nonexistent_dir_xyz/none"));
    EXPECT_FALSE(db.opened());
}

TEST(MetricsDbTest, TaskUpsertOverwritesLatestState) {
    TempDir tmp("task");
    fly::MetricsDb db;
    ASSERT_TRUE(db.open(tmp.path()));

    fly::TaskRow row;
    row.task_id_ = 7;
    row.name_ = "chain_stage";
    row.module_ = "test";
    row.status_ = "PENDING";
    row.created_ms_ = 1000;
    row.dbs_ = "/tmp/a.db,/tmp/b.db";
    db.record_task(row);
    db.record_task_event(7, 0, "SUBMIT", "name=chain_stage");

    // 状态推进：RUNNING（带 worker 与调度链时间）。
    row.status_ = "RUNNING";
    row.worker_id_ = 3;
    row.started_ms_ = 1200;
    row.ready_ms_ = 1100;
    db.record_task(row);

    // 终态：COMPLETED（带 worker 上报的执行窗口与资源/IO 指标）。
    row.status_ = "COMPLETED";
    row.completed_ms_ = 2000;
    row.exec_start_ms_ = 1250;
    row.exec_end_ms_ = 1980;
    row.cpu_time_ms_ = 400;
    row.read_time_ms_ = 120;
    row.write_time_ms_ = 60;
    row.read_bytes_ = 999;
    row.write_bytes_ = 777;
    row.mem_baseline_bytes_ = 100;
    row.mem_avg_bytes_ = 150;
    row.mem_peak_bytes_ = 300;
    db.record_task(row);
    db.close();

    const std::string path = tmp.path() + "/monitor.db";
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM tasks"), 1);  // UPSERT 不增行
    EXPECT_EQ(query_text(path, "SELECT status FROM tasks WHERE task_id=7"), "COMPLETED");
    EXPECT_EQ(query_text(path, "SELECT name FROM tasks WHERE task_id=7"), "chain_stage");
    EXPECT_EQ(query_int(path, "SELECT worker_id FROM tasks WHERE task_id=7"), 3);
    EXPECT_EQ(query_int(path, "SELECT exec_end_ms FROM tasks WHERE task_id=7"), 1980);
    EXPECT_EQ(query_int(path, "SELECT cpu_time_ms FROM tasks WHERE task_id=7"), 400);
    EXPECT_EQ(query_int(path, "SELECT read_bytes FROM tasks WHERE task_id=7"), 999);
    EXPECT_EQ(query_int(path, "SELECT mem_peak_bytes FROM tasks WHERE task_id=7"), 300);
    EXPECT_EQ(query_text(path, "SELECT dbs FROM tasks WHERE task_id=7"),
              "/tmp/a.db,/tmp/b.db");
    EXPECT_EQ(query_int(path,
              "SELECT COUNT(*) FROM events WHERE category='task' AND task_id=7"), 1);
}

TEST(MetricsDbTest, WorkerSamplesBatchAndIdempotentResend) {
    TempDir tmp("samples");
    fly::MetricsDb db;
    ASSERT_TRUE(db.open(tmp.path()));

    fly::CMVector<fly::MonitorSample> batch;
    for (int i = 0; i < 5; ++i) {
        fly::MonitorSample s;
        s.epoch_ms_ = 1000 + i;
        s.proc_rss_bytes_ = 100 * (i + 1);
        s.proc_cpu_bps_ = 2500;
        s.host_cpu_bps_ = 4300;
        s.host_mem_total_bytes_ = 64ull << 30;
        s.host_mem_avail_bytes_ = 32ull << 30;
        s.host_load1_x100_ = 150;
        s.net_read_bytes_ = 1000 * i;
        s.net_write_bytes_ = 500 * i;
        batch.push_back(s);
    }
    db.record_worker_samples(2, batch);
    // 成组补发语义：同一批重复入队必须幂等（INSERT OR IGNORE）。
    db.record_worker_samples(2, batch);
    db.record_worker_registered(2, "host_a", "10.0.0.1", "hybrid", "gpu,fast");
    db.record_worker_event(2, "DEAD", "heartbeat timeout");
    db.close();

    const std::string path = tmp.path() + "/monitor.db";
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM worker_samples"), 5);
    EXPECT_EQ(query_int(path, "SELECT proc_rss_bytes FROM worker_samples "
                              "WHERE worker_id=2 AND epoch_ms=1004"), 500);
    EXPECT_EQ(query_int(path, "SELECT net_write_bytes FROM worker_samples "
                              "WHERE worker_id=2 AND epoch_ms=1003"), 1500);
    EXPECT_EQ(query_text(path, "SELECT hostname FROM workers WHERE worker_id=2"), "host_a");
    EXPECT_EQ(query_text(path, "SELECT attributes FROM workers WHERE worker_id=2"),
              "gpu,fast");
    EXPECT_EQ(query_text(path, "SELECT last_event FROM workers WHERE worker_id=2"), "DEAD");
    // 注册 + DEAD 两条 worker 事件。
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM events WHERE category='worker'"), 2);
}

TEST(MetricsDbTest, ObjectIoBatch) {
    TempDir tmp("objio");
    fly::MetricsDb db;
    ASSERT_TRUE(db.open(tmp.path()));

    fly::CMVector<fly::ObjectIoRecord> records;
    fly::ObjectIoRecord r1;
    r1.epoch_ms_ = 111;
    r1.task_id_ = 9;
    r1.worker_id_ = 4;
    r1.is_write_ = false;
    r1.object_name_ = "/tmp/x.db:obj_a";
    r1.bytes_ = 4096;
    r1.duration_ms_ = 12;
    records.push_back(r1);
    fly::ObjectIoRecord r2;
    r2.epoch_ms_ = 222;
    r2.task_id_ = 9;
    r2.worker_id_ = 4;
    r2.is_write_ = true;
    r2.object_name_ = "/tmp/x.db:obj_b";
    r2.bytes_ = 2048;
    r2.duration_ms_ = 5;
    records.push_back(r2);
    db.record_object_io(records);
    db.record_event("db", "DB_FROZEN", 0, 9, "/tmp/x.db");
    db.close();

    const std::string path = tmp.path() + "/monitor.db";
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM object_io"), 2);
    EXPECT_EQ(query_text(path, "SELECT direction FROM object_io WHERE object_name="
                              "'/tmp/x.db:obj_a'"), "r");
    EXPECT_EQ(query_int(path, "SELECT duration_ms FROM object_io WHERE bytes=2048"), 5);
    EXPECT_EQ(query_text(path, "SELECT event FROM events WHERE category='db'"), "DB_FROZEN");
}

TEST(MetricsDbTest, ConcurrentEnqueueFromFourThreads) {
    TempDir tmp("concurrent");
    fly::MetricsDb db;
    ASSERT_TRUE(db.open(tmp.path()));

    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;
    std::vector<std::thread> threads;
    std::atomic<int> failures{0};
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kPerThread; ++i) {
                fly::MonitorSample s;
                s.epoch_ms_ = static_cast<uint64_t>(t) * 100000 + i;
                s.proc_rss_bytes_ = 1;
                fly::CMVector<fly::MonitorSample> batch{s};
                db.record_worker_samples(t + 1, batch);
                fly::TaskRow row;
                row.task_id_ = static_cast<uint64_t>(t) * 100000 + i;
                row.status_ = "COMPLETED";
                db.record_task(row);
            }
        });
    }
    for (auto& th : threads) th.join();
    db.close();

    const std::string path = tmp.path() + "/monitor.db";
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM worker_samples"),
              kThreads * kPerThread);
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM tasks"), kThreads * kPerThread);
}

TEST(MetricsDbTest, SubmitStormBatchedIntoCommittedRows) {
    TempDir tmp("storm");
    fly::MetricsDb db;
    ASSERT_TRUE(db.open(tmp.path()));

    // 千条提交风暴：入队快、close 全部落盘（写线程批量合并事务）。
    for (int i = 0; i < 1000; ++i) {
        fly::TaskRow row;
        row.task_id_ = static_cast<uint64_t>(i);
        row.name_ = "storm_task";
        row.status_ = "PENDING";
        db.record_task(row);
        db.record_task_event(i, 0, "SUBMIT");
    }
    db.close();

    const std::string path = tmp.path() + "/monitor.db";
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM tasks"), 1000);
    EXPECT_EQ(query_int(path, "SELECT COUNT(*) FROM events WHERE event='SUBMIT'"), 1000);
}

}  // namespace
