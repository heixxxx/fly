#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/graceful_shutdown.h>
#include <csignal>
#include <mutex>
#include <condition_variable>
#include <agent/cpp/worker_agent.h>
#include <common/testing/cpp/test_helpers.h>
#include "test_log_isolation.h"
#include <core/cpp/config.h>
#include <core/cpp/process_info.h>
#include <common/serialization/cpp/serialization_macros.h>
#include <thread>
#include <chrono>
#include <latch>
#include <algorithm>

using namespace fly::test;

// db_path 废弃：db_path 现在是 db_path 别名（不含 ':'）。db32 生成不含 ':' 的测试 db_path。
static CMString db32(const CMString& hint) {
    return "/test/" + hint;
}

namespace fly {

// 构造 TaskComplete 上报：stop() 前终结 RUNNING task 用（正常收尾语义的 drain
// 无硬 deadline——30s 超时已废除，留下未完成 task 的测试会死等到 bazel 超时）。
static void complete_task_for_stop(MasterAgent& master, uint64_t task_id,
                                   uint64_t worker_id) {
    TaskCompleteMessage complete;
    complete.task_id_ = task_id;
    complete.worker_id_ = worker_id;
    master.on_task_complete(0, complete);
}

TEST(MasterAgentTest, CreateAndStart) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    
    wait_for_running(master, true);
    EXPECT_TRUE(master.is_running());
    EXPECT_GT(master.get_port(), 0);
    master.stop();
    wait_for_running(master, false);
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, CreateWithDifferentPorts) {
    MasterAgent master1("127.0.0.1", 0);
    MasterAgent master2("127.0.0.1", 0);
    
    master1.start();
    master2.start();
    
    wait_for_running(master1, true);
    wait_for_running(master2, true);
    
    EXPECT_TRUE(master1.is_running());
    EXPECT_TRUE(master2.is_running());
    EXPECT_GT(master1.get_port(), 0);
    EXPECT_GT(master2.get_port(), 0);
    EXPECT_NE(master1.get_port(), master2.get_port());
    
    master1.stop();
    master2.stop();
    
    wait_for_running(master1, false);
    wait_for_running(master2, false);
    
    EXPECT_FALSE(master1.is_running());
    EXPECT_FALSE(master2.is_running());
}

// 重启端口策略：port 0 时每次 start 拿全新临时端口（不复用上次绑定结果），
// 固定端口时重启仍绑定用户指定端口。
TEST(MasterAgentTest, RestartUsesRequestedPortNotLastBound) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true, 100, 20);
    int port1 = master.get_port();
    ASSERT_GT(port1, 0);
    master.stop();
    wait_for_running(master, false, 100, 20);

    master.start();
    wait_for_running(master, true, 100, 20);
    // 必须成功绑定（回归：曾复用上次端口，close→rebind 窗口被并发进程抢占即
    // "Failed to create listen socket"）。port 0 时新旧端口由内核分配，不做相等断言。
    EXPECT_GT(master.get_port(), 0);
    master.stop();
    wait_for_running(master, false, 100, 20);

    // 固定端口重启：仍绑定用户指定端口。
    MasterAgent fixed("127.0.0.1", 28457);
    fixed.start();
    wait_for_running(fixed, true, 100, 20);
    EXPECT_EQ(fixed.get_port(), 28457);
    fixed.stop();
    wait_for_running(fixed, false, 100, 20);
    fixed.start();
    wait_for_running(fixed, true, 100, 20);
    EXPECT_EQ(fixed.get_port(), 28457);
    fixed.stop();
    wait_for_running(fixed, false, 100, 20);
}

TEST(MasterAgentTest, MultipleStartStop) {
    MasterAgent master("127.0.0.1", 0);
    
    master.start();
    wait_for_running(master, true);
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    wait_for_running(master, false);
    EXPECT_FALSE(master.is_running());
    
    master.start();
    wait_for_running(master, true);
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    wait_for_running(master, false);
    EXPECT_FALSE(master.is_running());
}

}  // namespace fly

#include <storage/cpp/data_service.h>
#include <storage/cpp/local_index.h>
#include <common/runtime/cpp/worker_context.h>
#include <storage/cpp/db_meta.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include <sqlite3.h>

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fly::test::qa_tmp_dir("fly_test_master");
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::filesystem::remove_all(path_);
    }
    const CMString& path() const { return path_; }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
private:
    CMString path_;
};

void create_idx_file(const CMString& db_path, const CMString& writer_id,
                     const CMVector<IndexEntry>& entries) {
    CMString idx_path = db_path + "/" + writer_id + ".idx";
    LocalIndex idx(idx_path);
    for (const auto& e : entries) {
        idx.add_entry(e);
    }
    idx.save();
}

} // anonymous namespace

namespace fly {

// --- setup_write_context ---

TEST(MasterAgentTest, SetupWriteContext_ActivatesWorkerAgentContext) {
    WorkerAgentContext::clear();
    EXPECT_FALSE(WorkerAgentContext::is_active());

    MasterAgent master("127.0.0.1", 0);
    master.setup_write_context();

    EXPECT_TRUE(WorkerAgentContext::is_active());
    EXPECT_NE(WorkerAgentContext::current_record_func(), nullptr);

    WorkerAgentContext::clear();
}

TEST(MasterAgentTest, SetupWriteContext_MasterRunning_RegisterWriteUpdatesRemoteIdx) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    CMString db_path = db32("test_db_setup_write");
    CMString obj_name = "test_obj_setup_write";
    CMString full_name = db_path + ":" + obj_name;

    // master 自写走 register 路径（on_master_register_write → do_write_register）。
    // record_write_func 现为 no-op，不再触发 placement 更新。
    auto [msg, err_type] = WorkerAgentContext::register_write(db_path, obj_name, 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);

    // do_write_register 应已更新 remote_idx（worker_id=0 = master 自写）
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_name));
    auto info = DataService::instance()->lookup_remote_idx(full_name);
    EXPECT_EQ(info.worker_id_, 0u);
    EXPECT_EQ(DataService::instance()->get_remote_size(full_name), 100);

    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();

    // Cleanup singleton state
    DataService::instance()->remove_remote_index(full_name);
}

// 预许可（§14.1 注册时序，测试 48）：preliminary register 成功但【不激活
// 可见性】——remote_idx 无登记、size 无记录；数据可见性等完成登记。
// 两次调用间保持同一 hash（真实流由 open_write_stream 保证——预许可设定、
// 完成登记复用）。
TEST(MasterAgentTest, PreliminaryRegister_NoVisibilityActivation) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);
    WorkerAgentContext::set_current_write_hash("prereg_test_hash");

    CMString db_path = db32("test_db_prereg");
    CMString obj_name = "test_obj_prereg";
    CMString full_name = db_path + ":" + obj_name;

    // 预许可：许可通过（UNKNOWN = 无错误）。
    auto [msg, err_type] = WorkerAgentContext::register_write(db_path, obj_name, 0,
                                                               /*preliminary=*/true);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);

    // 可见性未激活：remote_idx 无该对象。
    EXPECT_FALSE(DataService::instance()->has_remote_location(full_name))
        << "preliminary register must NOT activate visibility";

    // 完成登记（非 preliminary，带真实 size）：可见性激活。
    auto [msg2, err_type2] = WorkerAgentContext::register_write(db_path, obj_name, 256,
                                                                 /*preliminary=*/false);
    EXPECT_EQ(err_type2, TaskErrorType::UNKNOWN);
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_name));
    EXPECT_EQ(DataService::instance()->get_remote_size(full_name), 256);

    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();

    // Cleanup singleton state
    DataService::instance()->remove_remote_index(full_name);
}

TEST(MasterAgentTest, SetupWriteContext_MasterNotRunning_RecordWriteNoOp) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    // Do NOT start — running_ is false
    master.setup_write_context();

    CMString db_path = db32("test_db_noop");
    CMString obj_name = "test_obj_noop";
    CMString full_name = db_path + ":" + obj_name;

    // master record_write_func 现为 no-op；register_write 在 !running_ 时返回 UNKNOWN 不登记
    WorkerAgentContext::record_write(db_path, obj_name, 100);

    // remote_idx should NOT be updated（master 未 running）
    EXPECT_FALSE(DataService::instance()->has_remote_location(full_name));

    WorkerAgentContext::clear();
}

TEST(MasterAgentTest, SetupWriteContext_ClearDeactivatesContext) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.setup_write_context();
    EXPECT_TRUE(WorkerAgentContext::is_active());

    WorkerAgentContext::clear();
    EXPECT_FALSE(WorkerAgentContext::is_active());
    EXPECT_EQ(WorkerAgentContext::current_record_func(), nullptr);
}

// --- restore_master_idx ---

// RunSummary（用户裁定输出形态）：stop 后写 {log_dir}/runtime.summary 与
// {log_dir}/db.summary，不直接展示 summary 内容。
TEST(MasterAgentTest, StopWritesSummaryFiles) {
    TempDir outdir;
    Config::instance()->set_str("log_dir", outdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    master.stop();
    wait_for_running(master, false);

    EXPECT_TRUE(std::filesystem::exists(outdir.path() + "/runtime.summary"));
    EXPECT_TRUE(std::filesystem::exists(outdir.path() + "/db.summary"));
}

// RunSummary + monitor.db：on_monitor_sample 的成组样本（真实 epoch 时刻，
// 含积压补发）完整进入 collector 与 worker_samples 表——组内每条都有价值，
// 不是只取最新一条；重复补发 DB 幂等。
TEST(MasterAgentTest, MonitorSampleGroupedSamplesCollected) {
    TempDir outdir;
    Config::instance()->set_str("log_dir", outdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    master.register_fake_worker_for_testing(77, 999);

    MonitorSampleMessage msg;
    msg.worker_id_ = 77;
    uint64_t now = RunMetricsCollector::epoch_ms_now();
    constexpr uint64_t kMB = 1024ull * 1024ull;
    for (int i = 0; i < 3; ++i) {
        MonitorSample s;
        s.epoch_ms_ = now - 20000 + static_cast<uint64_t>(i) * 10000;
        s.proc_rss_bytes_ = (100 + static_cast<uint64_t>(i) * 10) * kMB;
        s.proc_cpu_bps_ = 500;
        s.host_cpu_bps_ = 800;
        msg.samples_.push_back(s);
    }
    master.on_monitor_sample(msg);
    // 同组重复投递（补发语义）：RunMetrics 累计保留，DB 幂等不重。
    master.on_monitor_sample(msg);

    ASSERT_NE(master.run_metrics_for_testing(), nullptr);
    EXPECT_EQ(master.run_metrics_for_testing()->worker_sample_count_for_testing(77), 6u);
    ASSERT_NE(master.metrics_db_for_testing(), nullptr);
    EXPECT_TRUE(master.metrics_db_for_testing()->opened());

    master.unregister_fake_worker_for_testing(77, 999);
    master.stop();
    wait_for_running(master, false);

    // stop 同步 close 后文件为干净终态，可只读打开断言（重复组 INSERT OR IGNORE）。
    const std::string db_path = outdir.path() + "/monitor.db";
    ASSERT_TRUE(std::filesystem::exists(db_path));
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr),
              SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    // 按 worker_id=77 过滤：直调 on_monitor_sample 会触发 master 自身的
    // 事件采样（monitor_self_event 落 wid=0 行）——预期行为，不计入本断言。
    ASSERT_EQ(sqlite3_prepare_v2(db,
              "SELECT COUNT(*) FROM worker_samples WHERE worker_id=77", -1,
              &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int64(stmt, 0), 3);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}

TEST(MasterAgentTest, RestoreMasterIdx_ExistingIdxFile) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();

    // Create idx file with entries (LocalIndex stores short_name only)
    IndexEntry entry1;
    entry1.object_name_ = "obj_restore_1";
    entry1.file_name_ = "data_0.bin";
    entry1.offset_ = 0;
    entry1.size_ = 100;

    IndexEntry entry2;
    entry2.object_name_ = "obj_restore_2";
    entry2.file_name_ = "data_0.bin";
    entry2.offset_ = 100;
    entry2.size_ = 200;

    create_idx_file(db_path, "master000", {entry1, entry2});

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_path, "master000");

    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].object_name_, "obj_restore_1");
    EXPECT_EQ(entries[1].object_name_, "obj_restore_2");

    // DataService local_idx should be populated
    EXPECT_TRUE(DataService::instance()->has_local_object(db_path + ":obj_restore_1"));
    EXPECT_TRUE(DataService::instance()->has_local_object(db_path + ":obj_restore_2"));

    // Cleanup
    DataService::instance()->remove_local_index(db_path + ":obj_restore_1");
    DataService::instance()->remove_local_index(db_path + ":obj_restore_2");
}

TEST(MasterAgentTest, RestoreMasterIdx_NonExistentIdxFile) {
    TempDir tmpdir;
    CMString db_path = db32("test_db_restore_missing");

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_path, "w999");

    EXPECT_TRUE(entries.empty());
}

TEST(MasterAgentTest, RestoreMasterIdx_EmptyIdxFile) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();

    // Create idx file with no entries
    CMString idx_path = db_path + "/master000.idx";
    {
        // LocalIndex with no entries → save writes nothing (not modified)
        // So we need to touch the file manually to create an empty file
        std::ofstream ofs(idx_path, std::ios::binary);
        // Empty file
    }

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_path, "master000");

    EXPECT_TRUE(entries.empty());
}

TEST(MasterAgentTest, RestoreMasterIdx_MultipleEntries) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();

    // Create multiple entries (LocalIndex stores short_name only)
    CMVector<IndexEntry> entries_writer0;
    for (int i = 0; i < 5; i++) {
        IndexEntry e;
        e.object_name_ = fmt::format("multi_obj_{}", i);
        e.file_name_ = "data_0.bin";
        e.offset_ = i * 100;
        e.size_ = 100;
        entries_writer0.push_back(e);
    }
    create_idx_file(db_path, "master000", entries_writer0);

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_path, "master000");

    ASSERT_EQ(entries.size(), 5u);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(entries[i].object_name_, fmt::format("multi_obj_{}", i));
    }

    // Cleanup
    for (int i = 0; i < 5; i++) {
        DataService::instance()->remove_local_index(db_path + ":" + fmt::format("multi_obj_{}", i));
    }
}

// load_db 可见性屏障状态机（盲 sleep 根除回归）：send 登记 → Ack+rebuild
// 递减 → 归 0 时对象位置已可见；Ack 失败 / unknown db_path 置 -1（等待侧
// 报错而非静默返回）；未发送的 db 恒 0；重复 Ack 防下溢。
// 端到端（QA backup_load_db_multi_worker 高负载 idx 加载 2.2s 越过
// sleep(1.0) 窗口）由 QA 覆盖。
// 判死联动收敛：数据规模相关等待已无限化（timeout<=0），无限等待的安全性由
// worker 判死事件驱动 pending 期待显式终结保证——死亡 worker 的 IdxLoad 期待
// 置 -1（load_db 侧显式报错）、DeleteData 期待 complete 失败（wait 侧收到
// success_=false 而非死等）。无 timeout 兜底的回归会被本用例以 hang 形式暴露。
TEST(MasterAgentTest, WorkerDeathSettlesPendingRpc) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    master.register_database(db_path, "");
    master.register_fake_worker_for_testing(7, 7001);

    // IdxLoad 期待：判死 → remaining 置 -1（显式失败，Python load_db 轮询 raise）。
    master.send_idx_load_to_worker(db_path, {"w1"}, 7);
    EXPECT_EQ(master.idx_load_pending(db_path), 1);
    master.handle_worker_death_for_testing(7);
    EXPECT_EQ(master.idx_load_pending(db_path), -1)
        << "判死后 IdxLoad 期待必须显式失败（-1），不能停留 1 死等";

    // DeleteData 期待：登记后再判死（settle 对新登记条目同样生效）→
    // complete(success_=false)；无限 wait（timeout=0）立即收到显式失败返回，
    // 不 hang。
    master.send_delete_data(7, db_path, tmpdir.path(), {"w1"});
    master.handle_worker_death_for_testing(7);
    CMVector<uint64_t> failed;
    bool ok = master.wait_delete_data_acks({7}, db_path, 0, &failed);
    EXPECT_FALSE(ok) << "判死 worker 的 delete ack 必须显式失败";
    ASSERT_EQ(failed.size(), 1u);
    EXPECT_EQ(failed[0], 7u);

    // 幂等：三次判死不破坏已终结状态（不抛错、不复活期待）。
    master.handle_worker_death_for_testing(7);
    EXPECT_EQ(master.idx_load_pending(db_path), -1);

    master.unregister_fake_worker_for_testing(7, 7001);
    master.stop();
    wait_for_running(master, false);
}

// failed_tasks 落点按归属 db（Task db 归属规则）：owner 非空 →
// {owner_db_path}/failed_tasks.bin；owner 空 → {log_dir} fallback。persist/remove
// 链路全部经 get_failed_tasks_file_path(owner)，一处规则全链生效。
TEST(MasterAgentTest, FailedTasksFilePerOwnerPath) {
    MasterAgent master("127.0.0.1", 0);
    TempDir tmpdir;
    CMString db_path = tmpdir.path() + "/owner_db";
    CMString log_dir = tmpdir.path() + "/log";
    Config::instance()->set_str("log_dir", log_dir);

    // owner 非空：落归属 db 目录。
    EXPECT_EQ(master.failed_tasks_file_path_for_testing(db_path),
              db_path + "/failed_tasks.bin");
    // owner 空：fallback log_dir。
    EXPECT_EQ(master.failed_tasks_file_path_for_testing(""),
              log_dir + "/failed_tasks.bin");

    // persist 实际落点按 record 归属（db 目录无需预先存在，防御性创建）。
    FailedTaskRecord record;
    record.task_id_ = 7;
    record.submission_.name_ = "owner_test";
    record.submission_.owner_db_path_ = db_path;
    master.persist_failed_task_for_testing(record);
    EXPECT_TRUE(std::filesystem::exists(db_path + "/failed_tasks.bin"))
        << "persist must land on the owner db path";
    EXPECT_FALSE(std::filesystem::exists(log_dir + "/failed_tasks.bin"))
        << "owned task must not pollute the log_dir fallback file";
}

// 位置即归属（location-carried ownership）：restart 读取时 owner 归一化为
// bin 所在目录（当前路径），记录内旧路径快照不外溢——db/project 目录迁移后
// 重投的 task 再失败落当前 bin 位置，不会在旧路径重建幽灵目录。
TEST(MasterAgentTest, RestartNormalizesOwnerToBinLocation) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    TempDir tmpdir;
    CMString old_path = tmpdir.path() + "/old_location";   // 迁移前路径（已不存在）
    CMString new_path = tmpdir.path() + "/new_location";   // bin 实际所在（迁移后）

    // bin 落在 new_path（模拟目录已被搬到新位置），记录内 owner 仍是旧快照。
    FailedTaskRecord record;
    record.task_id_ = 42;
    record.submission_.name_ = "migrated_task";
    record.submission_.args_ = {"arg1"};
    record.submission_.owner_db_path_ = old_path;

    CMString bin_path = new_path + "/failed_tasks.bin";
    std::filesystem::create_directories(new_path);
    {
        CMString body;
        FLY_ENCODE(record, body);
        int64_t body_size = static_cast<int64_t>(body.size());
        std::ofstream ofs(bin_path, std::ios::binary | std::ios::app);
        ofs.write(reinterpret_cast<const char*>(&body_size), sizeof(body_size));
        ofs.write(body.data(), body.size());
    }

    size_t restarted = master.restart_failed_tasks(bin_path);
    EXPECT_EQ(restarted, 1u);
    EXPECT_EQ(master.task_owner_db_path_for_testing(42), new_path)
        << "owner must be normalized to the bin's parent directory";
    EXPECT_FALSE(std::filesystem::exists(old_path))
        << "must not recreate the stale pre-migration directory";
    EXPECT_FALSE(std::filesystem::exists(bin_path))
        << "bin is consumed (read-then-delete) on restart";

    master.stop();
    wait_for_running(master, false);
}

// restart 按 uid 解析：记录内 db 引用是提交时路径快照，迁移后按运行时
// uid 索引命中当前路径——args 重编码 v2、inputs/vars 前缀替换、hash 保持
// 原值（provenance 相等比较语义，重算即被拒）。
TEST(MasterAgentTest, RestartResolvesDbByUid) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    TempDir tmpdir;
    CMString old_path = tmpdir.path() + "/old_loc";
    CMString new_path = tmpdir.path() + "/new_loc";

    master.register_db_uid("uid_deadbeef", new_path);

    FailedTaskRecord record;
    record.task_id_ = 7;
    record.submission_.name_ = "migrated_task";
    record.submission_.args_ = {"__fly_db2__:uid_deadbeef:" + old_path, "plain_arg"};
    record.submission_.inputs_ = {old_path + ":dep_obj"};
    record.submission_.vars_ = {old_path + ":some_var"};
    record.submission_.owner_db_path_ = old_path;

    CMString bin_path = new_path + "/failed_tasks.bin";
    std::filesystem::create_directories(new_path);
    {
        CMString body;
        FLY_ENCODE(record, body);
        int64_t body_size = static_cast<int64_t>(body.size());
        std::ofstream ofs(bin_path, std::ios::binary | std::ios::app);
        ofs.write(reinterpret_cast<const char*>(&body_size), sizeof(body_size));
        ofs.write(body.data(), body.size());
    }

    EXPECT_EQ(master.restart_failed_tasks(bin_path), 1u);

    auto spec = master.task_submission_for_testing(7);
    EXPECT_EQ(spec.args_[0], "__fly_db2__:uid_deadbeef:" + new_path)
        << "args must be re-encoded with the current db_path";
    EXPECT_EQ(spec.args_[1], "plain_arg");
    EXPECT_EQ(spec.inputs_[0], new_path + ":dep_obj")
        << "input prefixes must follow old→new";
    EXPECT_EQ(spec.vars_[0], new_path + ":some_var");
    EXPECT_EQ(spec.owner_db_path_, new_path)
        << "owner follows the bin location (current path)";

    master.stop();
    wait_for_running(master, false);
}

// 文件级原子：bin 内任一 db 引用无法按 uid 解析（未 load）→ 整个 bin 不重投，
// 文件完整保留（用户 load 该 db 后重试闭环）。
TEST(MasterAgentTest, RestartAtomicOnUnresolvedUid) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    TempDir tmpdir;
    CMString db_path = tmpdir.path() + "/loaded_db";
    CMString other_db = tmpdir.path() + "/unloaded_db";

    master.register_db_uid("uid_ok", db_path);   // 已 load
    // "uid_missing" 故意不注册。

    FailedTaskRecord r1;
    r1.task_id_ = 1;
    r1.submission_.name_ = "ok_task";
    r1.submission_.args_ = {"__fly_db2__:uid_ok:" + db_path};

    FailedTaskRecord r2;
    r2.task_id_ = 2;
    r2.submission_.name_ = "bad_task";
    r2.submission_.args_ = {"__fly_db2__:uid_missing:" + other_db};

    CMString bin_path = db_path + "/failed_tasks.bin";
    std::filesystem::create_directories(db_path);
    for (const auto& r : {r1, r2}) {
        CMString body;
        FLY_ENCODE(r, body);
        int64_t body_size = static_cast<int64_t>(body.size());
        std::ofstream ofs(bin_path, std::ios::binary | std::ios::app);
        ofs.write(reinterpret_cast<const char*>(&body_size), sizeof(body_size));
        ofs.write(body.data(), body.size());
    }

    EXPECT_EQ(master.restart_failed_tasks(bin_path), 0u)
        << "whole bin must be rejected when any db uid is unresolved";
    EXPECT_TRUE(std::filesystem::exists(bin_path))
        << "bin must be preserved for retry after loading the missing db";
    EXPECT_FALSE(master.task_submission_for_testing(1).name_ == "ok_task")
        << "no record from this bin may be resubmitted";

    // 补 load 缺失 db 后重试 → 整 bin 重投成功。
    master.register_db_uid("uid_missing", other_db);
    EXPECT_EQ(master.restart_failed_tasks(bin_path), 2u);
    EXPECT_FALSE(std::filesystem::exists(bin_path))
        << "bin is consumed once all uids resolve";

    master.stop();
    wait_for_running(master, false);
}

TEST(MasterAgentTest, IdxLoadPendingVisibilityBarrier) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    // 成功 Ack 的 rebuild 前置：db_instances_ 有条目 + idx 文件存在
    //（rebuild 走 LocalIndex 读文件 + update_remote_idx + mark_data_ready）。
    master.register_database(db_path, "");
    IndexEntry entry;
    entry.object_name_ = "obj_idxload_pending";
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 10;
    create_idx_file(db_path, "w1", {entry});

    EXPECT_EQ(master.idx_load_pending(db_path), 0) << "未发送过命令的 db 恒 0";

    // 发给不存在的 worker：conn==0 不登记。
    master.send_idx_load_to_worker(db_path, {"w1"}, 999);
    EXPECT_EQ(master.idx_load_pending(db_path), 0);

    master.register_fake_worker_for_testing(7, 7001);
    master.register_fake_worker_for_testing(8, 7002);

    IdxLoadAckMessage ack;
    ack.db_path_ = db_path;
    ack.loaded_writer_ids_ = {"w1"};
    ack.success_ = true;

    // 双 worker 发送 → 计数 2；首个 Ack 递减到 1（等待侧必须继续等）。
    master.send_idx_load_to_worker(db_path, {"w1"}, 7);
    master.send_idx_load_to_worker(db_path, {"w1"}, 8);
    EXPECT_EQ(master.idx_load_pending(db_path), 2);

    ack.worker_id_ = 7;
    master.inject_idx_load_ack_for_testing(ack);
    EXPECT_EQ(master.idx_load_pending(db_path), 1);

    // 重复 Ack（重放）不产生下溢。
    master.inject_idx_load_ack_for_testing(ack);
    EXPECT_EQ(master.idx_load_pending(db_path), 1);

    // 第二个 Ack 后归 0：rebuild 已落地（remote_idx 有该对象位置）。
    ack.worker_id_ = 8;
    master.inject_idx_load_ack_for_testing(ack);
    EXPECT_EQ(master.idx_load_pending(db_path), 0);
    EXPECT_FALSE(DataService::instance()->get_remote_workers(db_path + ":obj_idxload_pending").empty())
        << "计数归 0 时对象位置必须已可见";

    // 失败路径：Ack success=false 置 -1（load_db 报错而非静默返回）。
    master.send_idx_load_to_worker(db_path, {"w1"}, 7);
    EXPECT_EQ(master.idx_load_pending(db_path), 1);
    ack.worker_id_ = 7;
    ack.success_ = false;
    master.inject_idx_load_ack_for_testing(ack);
    EXPECT_EQ(master.idx_load_pending(db_path), -1);

    // unknown db_path 的 Ack 同样消化计数（防等待侧死等超时）。
    CMString unknown_db = tmpdir.path() + "_unknown";
    master.send_idx_load_to_worker(unknown_db, {"w1"}, 7);
    EXPECT_EQ(master.idx_load_pending(unknown_db), 1);
    ack.db_path_ = unknown_db;
    ack.success_ = true;
    master.inject_idx_load_ack_for_testing(ack);
    EXPECT_EQ(master.idx_load_pending(unknown_db), -1);

    master.unregister_fake_worker_for_testing(7, 7001);
    master.unregister_fake_worker_for_testing(8, 7002);
    master.stop();
    wait_for_running(master, false);
}

// --- rebuild_remote_idx ---

TEST(MasterAgentTest, RebuildRemoteIdx_MasterEntries) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    CMString full = db_path + ":master_obj_1";

    // Create master's idx with entries (LocalIndex stores short_name only)
    IndexEntry entry;
    entry.object_name_ = "master_obj_1";
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 50;

    create_idx_file(db_path, "master000", {entry});

    // WorkerInfo for master (worker_id=0, hostname="localhost")
    ::WorkerInfo master_worker;
    master_worker.worker_id_ = 0;
    master_worker.writer_id_ = "master000";
    master_worker.hostname_ = "localhost";

    MasterAgent master("127.0.0.1", 0);
    // Register a new worker on "localhost" so master's entries get mapped to it
    DataService::instance()->register_worker(10, "127.0.0.1", 9999);
    master.add_worker_hostname(10, "localhost");

    master.rebuild_remote_idx(db_path, {master_worker});

    // Master entries now map to the new worker on same hostname
    EXPECT_TRUE(DataService::instance()->has_remote_location(full));
    auto info = DataService::instance()->lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id_, 10u);
    EXPECT_EQ(info.host_, "127.0.0.1");
    EXPECT_EQ(info.port_, 9999u);

    // Cleanup
    DataService::instance()->remove_remote_index(full);
}

TEST(MasterAgentTest, RebuildRemoteIdx_WorkerEntries_NoNewWorkers_Skipped) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    CMString full = db_path + ":worker_obj_skip";

    // Create worker_5.idx with entries (LocalIndex stores short_name only)
    IndexEntry entry;
    entry.object_name_ = "worker_obj_skip";
    entry.file_name_ = "data_5.bin";
    entry.offset_ = 0;
    entry.size_ = 50;

    create_idx_file(db_path, "worker005", {entry});

    // WorkerInfo for old worker_id=5, but no new workers registered
    ::WorkerInfo old_worker;
    old_worker.worker_id_ = 5;
    old_worker.writer_id_ = "worker005";
    old_worker.hostname_ = "testhost_skipped";

    MasterAgent master("127.0.0.1", 0);
    master.rebuild_remote_idx(db_path, {old_worker});

    // No new workers with matching hostname → entry should NOT be in remote_idx
    EXPECT_FALSE(DataService::instance()->has_remote_location(full));
}

TEST(MasterAgentTest, RebuildRemoteIdx_MissingIdxFile_Skipped) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();

    // WorkerInfo for a worker whose idx file doesn't exist
    ::WorkerInfo missing_worker;
    missing_worker.worker_id_ = 42;
    missing_worker.writer_id_ = "worker042";
    missing_worker.hostname_ = "ghost_host";

    MasterAgent master("127.0.0.1", 0);
    // Should not crash, just WARN and skip
    master.rebuild_remote_idx(db_path, {missing_worker});
}

TEST(MasterAgentTest, RebuildRemoteIdx_MultipleWorkers) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    CMString full_master = db_path + ":multi_master_obj";
    CMString full_worker = db_path + ":multi_worker_obj";

    // Create master's idx (LocalIndex stores short_name only)
    IndexEntry master_entry;
    master_entry.object_name_ = "multi_master_obj";
    master_entry.file_name_ = "data_0.bin";
    master_entry.offset_ = 0;
    master_entry.size_ = 50;
    create_idx_file(db_path, "master000", {master_entry});

    // Create worker's idx
    IndexEntry worker_entry;
    worker_entry.object_name_ = "multi_worker_obj";
    worker_entry.file_name_ = "data_3.bin";
    worker_entry.offset_ = 0;
    worker_entry.size_ = 100;
    create_idx_file(db_path, "worker003", {worker_entry});

    ::WorkerInfo w0;
    w0.worker_id_ = 0;
    w0.writer_id_ = "master000";
    w0.hostname_ = "master_host";

    ::WorkerInfo w3;
    w3.worker_id_ = 3;
    w3.writer_id_ = "worker003";
    w3.hostname_ = "unknown_host";

    MasterAgent master("127.0.0.1", 0);
    // Register a new worker on "master_host" for master's entries
    DataService::instance()->register_worker(10, "127.0.0.1", 9999);
    master.add_worker_hostname(10, "master_host");

    master.rebuild_remote_idx(db_path, {w0, w3});

    // master entries → mapped to new worker_id=10 on "master_host"
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_master));
    auto info0 = DataService::instance()->lookup_remote_idx(full_master);
    EXPECT_EQ(info0.worker_id_, 10u);
    EXPECT_EQ(info0.host_, "127.0.0.1");
    EXPECT_EQ(info0.port_, 9999u);

    // worker_id=3 entries → no matching new worker on "unknown_host" → skipped
    EXPECT_FALSE(DataService::instance()->has_remote_location(full_worker));

    // Cleanup
    DataService::instance()->remove_remote_index(full_master);
}

TEST(MasterAgentTest, RebuildRemoteIdx_EmptyWorkers_Noop) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();

    MasterAgent master("127.0.0.1", 0);
    // Empty workers vector → no iteration, no crash
    master.rebuild_remote_idx(db_path, {});
}

TEST(MasterAgentTest, RebuildRemoteIdx_MultiHost_MappedToCorrectWorkers) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    CMString full_m = db_path + ":master_data";
    CMString full_a = db_path + ":worker_a_data";
    CMString full_b = db_path + ":worker_b_data";

    // Master (worker_id=0) on "host_master" (LocalIndex stores short_name only)
    IndexEntry master_entry;
    master_entry.object_name_ = "master_data";
    master_entry.file_name_ = "data_m.bin";
    master_entry.offset_ = 0;
    master_entry.size_ = 50;
    create_idx_file(db_path, "w_master", {master_entry});

    // Worker A (worker_id=1) on "host_a"
    IndexEntry worker_a_entry;
    worker_a_entry.object_name_ = "worker_a_data";
    worker_a_entry.file_name_ = "data_a.bin";
    worker_a_entry.offset_ = 0;
    worker_a_entry.size_ = 80;
    create_idx_file(db_path, "w_hosta", {worker_a_entry});

    // Worker B (worker_id=2) on "host_b"
    IndexEntry worker_b_entry;
    worker_b_entry.object_name_ = "worker_b_data";
    worker_b_entry.file_name_ = "data_b.bin";
    worker_b_entry.offset_ = 0;
    worker_b_entry.size_ = 120;
    create_idx_file(db_path, "w_hostb", {worker_b_entry});

    ::WorkerInfo w_master;
    w_master.worker_id_ = 0;
    w_master.writer_id_ = "w_master";
    w_master.hostname_ = "host_master";

    ::WorkerInfo w_a;
    w_a.worker_id_ = 1;
    w_a.writer_id_ = "w_hosta";
    w_a.hostname_ = "host_a";

    ::WorkerInfo w_b;
    w_b.worker_id_ = 2;
    w_b.writer_id_ = "w_hostb";
    w_b.hostname_ = "host_b";

    MasterAgent master("127.0.0.1", 0);

    DataService::instance()->register_worker(100, "10.0.0.1", 8001);
    master.add_worker_hostname(100, "host_master");
    DataService::instance()->register_worker(200, "10.0.0.2", 8002);
    master.add_worker_hostname(200, "host_a");
    DataService::instance()->register_worker(300, "10.0.0.3", 8003);
    master.add_worker_hostname(300, "host_b");

    master.rebuild_remote_idx(db_path, {w_master, w_a, w_b});

    // master → worker 100 on host_master
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_m));
    auto info_m = DataService::instance()->lookup_remote_idx(full_m);
    EXPECT_EQ(info_m.worker_id_, 100u);
    EXPECT_EQ(info_m.host_, "10.0.0.1");
    EXPECT_EQ(info_m.port_, 8001u);

    // worker A → worker 200 on host_a
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_a));
    auto info_a = DataService::instance()->lookup_remote_idx(full_a);
    EXPECT_EQ(info_a.worker_id_, 200u);
    EXPECT_EQ(info_a.host_, "10.0.0.2");
    EXPECT_EQ(info_a.port_, 8002u);

    // worker B → worker 300 on host_b
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_b));
    auto info_b = DataService::instance()->lookup_remote_idx(full_b);
    EXPECT_EQ(info_b.worker_id_, 300u);
    EXPECT_EQ(info_b.host_, "10.0.0.3");
    EXPECT_EQ(info_b.port_, 8003u);

    DataService::instance()->remove_remote_index(full_m);
    DataService::instance()->remove_remote_index(full_a);
    DataService::instance()->remove_remote_index(full_b);
}

TEST(MasterAgentTest, RebuildRemoteIdx_SameHostMasterAndWorker_Merged) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    CMString full_m = db_path + ":m_obj";
    CMString full_w = db_path + ":w_obj";
    CMString full_r = db_path + ":r_obj";

    // Master (worker_id=0) on "host_local" (LocalIndex stores short_name only)
    IndexEntry master_entry;
    master_entry.object_name_ = "m_obj";
    master_entry.file_name_ = "data_m.bin";
    master_entry.offset_ = 0;
    master_entry.size_ = 50;
    create_idx_file(db_path, "w_m", {master_entry});

    // Worker (worker_id=5) on same "host_local"
    IndexEntry worker_entry;
    worker_entry.object_name_ = "w_obj";
    worker_entry.file_name_ = "data_w.bin";
    worker_entry.offset_ = 0;
    worker_entry.size_ = 80;
    create_idx_file(db_path, "w_w5", {worker_entry});

    // Remote Worker (worker_id=3) on "host_remote"
    IndexEntry remote_entry;
    remote_entry.object_name_ = "r_obj";
    remote_entry.file_name_ = "data_r.bin";
    remote_entry.offset_ = 0;
    remote_entry.size_ = 120;
    create_idx_file(db_path, "w_r3", {remote_entry});

    ::WorkerInfo w_m;
    w_m.worker_id_ = 0;
    w_m.writer_id_ = "w_m";
    w_m.hostname_ = "host_local";

    ::WorkerInfo w5;
    w5.worker_id_ = 5;
    w5.writer_id_ = "w_w5";
    w5.hostname_ = "host_local";

    ::WorkerInfo w3;
    w3.worker_id_ = 3;
    w3.writer_id_ = "w_r3";
    w3.hostname_ = "host_remote";

    MasterAgent master("127.0.0.1", 0);

    DataService::instance()->register_worker(10, "192.168.1.1", 9001);
    master.add_worker_hostname(10, "host_local");
    DataService::instance()->register_worker(20, "192.168.1.2", 9002);
    master.add_worker_hostname(20, "host_remote");

    master.rebuild_remote_idx(db_path, {w_m, w5, w3});

    // Both master and worker_id=5 on host_local → mapped to worker 10
    auto info_m = DataService::instance()->lookup_remote_idx(full_m);
    EXPECT_EQ(info_m.worker_id_, 10u);
    EXPECT_EQ(info_m.host_, "192.168.1.1");
    EXPECT_EQ(info_m.port_, 9001u);

    auto info_w = DataService::instance()->lookup_remote_idx(full_w);
    EXPECT_EQ(info_w.worker_id_, 10u);
    EXPECT_EQ(info_w.host_, "192.168.1.1");
    EXPECT_EQ(info_w.port_, 9001u);

    // Remote worker on host_remote → mapped to worker 20
    auto info_r = DataService::instance()->lookup_remote_idx(full_r);
    EXPECT_EQ(info_r.worker_id_, 20u);
    EXPECT_EQ(info_r.host_, "192.168.1.2");
    EXPECT_EQ(info_r.port_, 9002u);

    DataService::instance()->remove_remote_index(full_m);
    DataService::instance()->remove_remote_index(full_w);
    DataService::instance()->remove_remote_index(full_r);
}

TEST(MasterAgentTest, RebuildRemoteIdx_PartialHostCoverage) {
    TempDir tmpdir;
    CMString db_path = tmpdir.path();
    CMString full_avail = db_path + ":avail_obj";
    CMString full_off = db_path + ":offline_obj";

    // Worker A on "host_available" (LocalIndex stores short_name only)
    IndexEntry entry_a;
    entry_a.object_name_ = "avail_obj";
    entry_a.file_name_ = "data_a.bin";
    entry_a.offset_ = 0;
    entry_a.size_ = 50;
    create_idx_file(db_path, "w_avail", {entry_a});

    // Worker B on "host_offline"
    IndexEntry entry_b;
    entry_b.object_name_ = "offline_obj";
    entry_b.file_name_ = "data_b.bin";
    entry_b.offset_ = 0;
    entry_b.size_ = 80;
    create_idx_file(db_path, "w_off", {entry_b});

    ::WorkerInfo wa;
    wa.worker_id_ = 1;
    wa.writer_id_ = "w_avail";
    wa.hostname_ = "host_available";

    ::WorkerInfo wb;
    wb.worker_id_ = 2;
    wb.writer_id_ = "w_off";
    wb.hostname_ = "host_offline";

    MasterAgent master("127.0.0.1", 0);

    // Only register a worker for "host_available", NOT for "host_offline"
    DataService::instance()->register_worker(50, "10.0.0.10", 7000);
    master.add_worker_hostname(50, "host_available");

    master.rebuild_remote_idx(db_path, {wa, wb});

    // host_available → mapped
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_avail));
    auto info = DataService::instance()->lookup_remote_idx(full_avail);
    EXPECT_EQ(info.worker_id_, 50u);

    // host_offline → skipped (no worker registered)
    EXPECT_FALSE(DataService::instance()->has_remote_location(full_off));

    DataService::instance()->remove_remote_index(full_avail);
}

// --- Task failure rescheduling tests ---

TEST(MasterAgentTest, OnTaskFailedRecordsErrorAndUpdatesStatus) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // Submit two tasks with unsatisfied inputs so they stay in the graph
    master.submit_task(100, "task_a", "test_module", {"arg1"}, {"missing_input_1"}, {});
    master.submit_task(101, "task_b", "test_module", {"arg2"}, {"missing_input_2"}, {});

    // Tasks should exist (either pending or failed)
    auto failed = master.get_failed_tasks();
    auto pending = master.get_pending_tasks();
    EXPECT_GE(failed.size() + pending.size(), 2u);

    master.stop();
    wait_for_running(master, false);
}

// With fail_unscheduleable_tasks=1, a ready task requiring capabilities that no
// worker has is immediately failed. Covers schedule_tasks capability-mismatch
// branch (master_agent.cpp L322-363).
TEST(MasterAgentTest, UnscheduleableCapabilityTaskFailsImmediately) {
    Config::instance()->set_int("fail_unscheduleable_tasks", 1);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // Task with no input deps (becomes ready) but requires a capability no worker has.
    master.submit_task(200, "cap_task", "mod", {}, {}, {}, {"gpu"});

    // Wait briefly for schedule_tasks to process the unscheduleable task.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto failed = master.get_failed_tasks();
    EXPECT_GE(failed.size(), 1u)
        << "capability-unscheduleable task should be failed immediately";

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("fail_unscheduleable_tasks", 0);
}

// With fail_unscheduleable_tasks=1, pending tasks whose dependencies will never
// be satisfied (deadlock) are detected and failed. Covers schedule_tasks
// deadlock-detection branch (master_agent.cpp L366-401).
TEST(MasterAgentTest, DeadlockedPendingTasksAreDetectedAndFailed) {
    Config::instance()->set_int("fail_unscheduleable_tasks", 1);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // Pending task depending on an input that no task will ever produce.
    master.submit_task(300, "deadlock_task", "mod", {"a"}, {"never_produced_input"}, {}, {});

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto failed = master.get_failed_tasks();
    EXPECT_GE(failed.size(), 1u)
        << "deadlocked pending task should be detected and failed";

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("fail_unscheduleable_tasks", 0);
}

TEST(MasterAgentTest, StopDuringActiveCommunication) {
    fly::DataService::instance()->reset();
    // Regression test for bd1e5df: MasterAgent::stop() accessed conn_to_worker_ maps
    // while reactor thread still active, causing segfault.
    // Fix: stop reactor before accessing maps.
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // Stop master while worker is connected — should not segfault
    master.stop();
    wait_for_running(master, false);

    worker.stop();

    // Cleanup
    fly::DataService::instance()->stop_data_server();
}

// 2026-08-16 冗余清理：DoubleStopNoCrash / StopBeforeStartNoCrash 已被
// StopIsIdempotent / StopBeforeStart_CallsDoDrainAndStop（更新一代，断言更强）取代。

TEST(MasterAgentTest, OnDisconnectRecoversRunningTasks) {
    Logger::shutdown();
    Logger::init("test_logs/", 0);
    fly::DataService::instance()->reset();
    Config::instance()->set_int("worker_reconnect_timeout", 120);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.submit_task(42, "recovery_task", "test_module", {"arg1"}, {}, {});

    auto running = master.get_running_tasks();
    bool task_is_running = false;
    for (auto id : running) {
        if (id == 42) { task_is_running = true; break; }
    }

    if (!task_is_running) {
        wait_for([&]{
            auto r = master.get_running_tasks();
            for (auto id : r) { if (id == 42) return true; }
            return false;
        }, 50, 20);
        running = master.get_running_tasks();
        for (auto id : running) {
            if (id == 42) { task_is_running = true; break; }
        }
    }

    ASSERT_TRUE(task_is_running) << "Task 42 must reach RUNNING for disconnect recovery test";

    // 模拟网络闪断（master 视角）：只断 TCP、不退出 worker——WorkerAgent.stop()
    // 会声明 graceful 退出（WORKER_EXIT → exit 分派 fail RUNNING，用户裁定
    // 语义），与宽限恢复语义相斥；闪断用 simulate 钩子 + park 重连线程。
    std::mutex hk_m;
    std::condition_variable hk_cv;
    bool release_reconnect = false;
    worker.reconnect_entry_hook_for_testing_ = [&] {
        std::unique_lock<std::mutex> lk(hk_m);
        hk_cv.wait_for(lk, std::chrono::seconds(10), [&] { return release_reconnect; });
    };
    worker.simulate_master_disconnect_for_testing();
    wait_for([&]{
        // 等 on_disconnect 完成（连接表移除）。
        auto workers = master.get_connected_workers();
        return workers.empty();
    }, 100, 30);
    {
        auto r = master.get_running_tasks();
        bool still = false;
        for (auto id : r) { if (id == 42) { still = true; break; } }
        EXPECT_TRUE(still) << "Task 42 must stay RUNNING during grace period";
        EXPECT_TRUE(master.get_idle_workers().empty())
            << "Disconnected-but-graced worker must not be schedulable";
    }

    // ── 宽限超时 → 判死 → task 重排队（原断连恢复语义）──
    // 确定性等待：on_disconnect 的清表与宽限登记之间有中间代码（见
    // grace_workers_for_testing 注释）——等宽限表非空再驱动超时判死。
    wait_for([&]{ return !master.grace_workers_for_testing().empty(); }, 100, 30);
    master.check_grace_deadlines_for_testing(9999999999LL);

    wait_for([&]{
        auto r = master.get_running_tasks();
        bool found = false;
        for (auto id : r) { if (id == 42) { found = true; break; } }
        return !found;
    }, 100, 30);

    auto running_after = master.get_running_tasks();
    bool still_running = false;
    for (auto id : running_after) {
        if (id == 42) { still_running = true; break; }
    }
    EXPECT_FALSE(still_running) << "Task 42 should no longer be RUNNING after grace expiry";

    auto failed = master.get_failed_tasks();
    bool task_failed = false;
    for (auto id : failed) {
        if (id == 42) { task_failed = true; break; }
    }
    EXPECT_FALSE(task_failed) << "Task 42 should be re-queued (not FAILED) after grace expiry";

    master.stop();
    wait_for_running(master, false);
    Logger::shutdown();
    Config::instance()->set_int("worker_reconnect_timeout", 120);
}

// 宽限内 IDLE worker 断连的调度排除由 worker_manager_test 的
// GraceFlagExcludesFromIdleCandidates / GraceClearedOnReconnect 精确验证
//（WorkerManager 状态机层）；master 侧接线（on_disconnect 置位 → 重连复位）
// 由 qa/fault/test_disconnect_reconnect_grace.py 行为级验证。
// 此处曾放真 WorkerAgent 版用例：WorkerAgent.stop() ≠ 进程死（同进程对象
// 会在 ~300ms 内自动重连），窗口竞态导致断言不稳定，故拆层。

// 宽限内重连：task 存活 + 迟到 Complete 正常收敛（防串扰 + BUSY 保留）。
TEST(MasterAgentTest, ReconnectWithinGracePreservesTask) {
    Logger::shutdown();
    Logger::init("test_logs/", 0);
    fly::DataService::instance()->reset();
    Config::instance()->set_int("worker_reconnect_timeout", 120);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    master.submit_task(43, "grace_task", "test_module", {"arg"}, {}, {});
    wait_for([&]{
        auto r = master.get_running_tasks();
        for (auto id : r) { if (id == 43) return true; }
        return false;
    }, 50, 20);

    // 模拟网络闪断（同上：simulate 钩子 + park 重连线程，不声明退出）。
    std::mutex hk_m;
    std::condition_variable hk_cv;
    bool release_reconnect = false;
    worker.reconnect_entry_hook_for_testing_ = [&] {
        std::unique_lock<std::mutex> lk(hk_m);
        hk_cv.wait_for(lk, std::chrono::seconds(10), [&] { return release_reconnect; });
    };
    worker.simulate_master_disconnect_for_testing();
    wait_for([&]{ return master.get_connected_workers().empty(); }, 100, 30);

    // worker 重连（新进程/新连接，同 worker_id）→ 宽限内重注册。
    WorkerAgent worker_again(1, "127.0.0.1", master.get_port());
    worker_again.start();
    ASSERT_TRUE(wait_until_registered(worker_again));

    // task 仍是 RUNNING（未被重排队）；worker 未被派新 task（BUSY 保留）。
    {
        auto r = master.get_running_tasks();
        bool still = false;
        for (auto id : r) { if (id == 43) { still = true; break; } }
        EXPECT_TRUE(still) << "Task 43 must stay RUNNING across reconnect within grace";
    }

    // 迟到 Complete（重连后的上报）：assigned worker 一致 → 正常收敛。
    TaskCompleteMessage complete;
    complete.task_id_ = 43;
    complete.worker_id_ = 1;
    master.on_task_complete(0, complete);
    {
        auto completed = master.get_completed_tasks();
        bool found = false;
        for (auto id : completed) { if (id == 43) { found = true; break; } }
        EXPECT_TRUE(found) << "Reconnected worker's report must complete the task";
    }

    worker_again.stop();
    master.stop();
    wait_for_running(master, false);
    Logger::shutdown();
}

// 数据全灭快速失败：依赖"全部 holder 判死"对象的等待 task 直接失败；
// 只死一个 holder（另一 holder 活着）不失败。
// 稳定 pending 的手法：先派两个占位 task 占满 worker（测试 worker 无 executor，
// task 派下即卡 RUNNING），依赖 task 无 idle worker 可派 → 停留队列。
TEST(MasterAgentTest, AllReplicasDeadFailsWaitingTasks) {
    Logger::shutdown();
    Logger::init("test_logs/", 0);
    fly::DataService::instance()->reset();
    Config::instance()->set_int("worker_reconnect_timeout", 120);
    Config::instance()->set_int("fail_unscheduleable_tasks", 0);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent w1(1, "127.0.0.1", master.get_port());
    WorkerAgent w2(2, "127.0.0.1", master.get_port());
    w1.start();
    w2.start();
    ASSERT_TRUE(wait_until_registered(w1));
    ASSERT_TRUE(wait_until_registered(w2));

    // 占满两个 worker（task 无 executor，保持 RUNNING/占用状态）。
    master.submit_task(40, "occupy_1", "test_module", {}, {}, {});
    master.submit_task(41, "occupy_2", "test_module", {}, {}, {});
    wait_for([&]{
        auto running = master.get_running_tasks();
        int n = 0;
        for (auto id : running) { if (id == 40 || id == 41) ++n; }
        return n >= 1;  // 至少一个被派下（局部性选择可能集中到同 worker，容忍）
    }, 50, 20);

    // 对象 D 双副本（worker1 + worker2）；对象 E 仅 worker1。
    CMString D = "/g2db:dual";
    CMString E = "/g2db:only";
    fly::DataService::instance()->update_remote_idx(D, 1, "127.0.0.1", 1000);
    fly::DataService::instance()->update_remote_idx(D, 2, "127.0.0.1", 1000);
    fly::DataService::instance()->update_remote_idx(E, 1, "127.0.0.1", 1000);
    master.mark_data_ready_for_testing(D);
    master.mark_data_ready_for_testing(E);

    // 依赖 D / E 的 task：数据 ready 但无空闲 worker → 停留调度队列（不被 assign）。
    master.submit_task(50, "dep_D", "test_module", {"a"}, {D}, {});
    master.submit_task(51, "dep_E", "test_module", {"a"}, {E}, {});
    // 确认依赖 task 未 RUNNING。
    wait_for([&]{
        auto running = master.get_running_tasks();
        for (auto id : running) {
            if (id == 50 || id == 51) return false;
        }
        return true;
    }, 10, 5);

    // 诊断辅助：dump 判死链关键状态（R9 稳定性失败取证：fail 执行但读不到）。
    auto diag = [&](const char* tag) {
        auto cs = master.get_connected_workers();
        auto pend = master.get_pending_tasks();
        auto run = master.get_running_tasks();
        auto fai = master.get_failed_tasks();
        CMString cs_s, pend_s, run_s, fai_s;
        for (auto w : cs) cs_s += std::to_string(w) + ",";
        for (auto id : pend) pend_s += std::to_string(id) + ",";
        for (auto id : run) run_s += std::to_string(id) + ",";
        for (auto id : fai) fai_s += std::to_string(id) + ",";
        ERR("[DIAG] {} connected=[{}] pending=[{}] running=[{}] failed=[{}]",
            tag, cs_s, pend_s, run_s, fai_s);
    };

    // 等待断连进入宽限登记（而非 connected empty）：on_disconnect 的清表与宽限
    // 登记之间有中间代码，负载下 lane 线程可在窗口被抢占——只等 connected
    // empty 就 check 会空转（R9 稳定性失败根因）。宽限登记是 check 的真前提。
    auto wait_in_grace = [&](uint64_t wid) {
        for (int i = 0; i < 100; ++i) {
            auto g = master.grace_workers_for_testing();
            if (std::find(g.begin(), g.end(), wid) != g.end()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        return false;
    };

    // ── worker1 异常死亡（闪断→宽限超时判死）→ D 仍有 worker2（活）→
    //    dep_D 不失败；E 全灭 → dep_E（队列中）直接失败（快速失败）。
    //    用 simulate 断连（不声明退出）：WorkerAgent.stop() 会发 WORKER_EXIT
    //    走正常退出路径（无 orphan fail，用户裁定语义），与被测判死链相斥。
    //    park 重连线程防 ~300ms 重连复位宽限。
    std::mutex hk1_m; std::condition_variable hk1_cv; bool rel1 = false;
    w1.reconnect_entry_hook_for_testing_ = [&] {
        std::unique_lock<std::mutex> lk(hk1_m);
        hk1_cv.wait_for(lk, std::chrono::seconds(10), [&] { return rel1; });
    };
    w1.simulate_master_disconnect_for_testing();
    EXPECT_TRUE(wait_in_grace(1)) << "worker1 disconnect not in grace registry within 3s (on_disconnect delayed)";
    diag("pre-check1");
    master.check_grace_deadlines_for_testing(9999999999LL);
    diag("post-check1");

    auto failed = master.get_failed_tasks();
    bool dep_D_failed = false, dep_E_failed = false;
    for (auto id : failed) {
        if (id == 50) dep_D_failed = true;
        if (id == 51) dep_E_failed = true;
    }
    EXPECT_FALSE(dep_D_failed) << "D still has a live holder (worker2) — dep task must NOT fail";
    EXPECT_TRUE(dep_E_failed) << "E lost its only holder — dependent waiting task must fast-fail";

    // ── worker2 也判死 → D 全灭 → dep_D 此时失败。──
    std::mutex hk2_m; std::condition_variable hk2_cv; bool rel2 = false;
    w2.reconnect_entry_hook_for_testing_ = [&] {
        std::unique_lock<std::mutex> lk(hk2_m);
        hk2_cv.wait_for(lk, std::chrono::seconds(10), [&] { return rel2; });
    };
    w2.simulate_master_disconnect_for_testing();
    EXPECT_TRUE(wait_in_grace(2)) << "worker2 disconnect not in grace registry within 3s (on_disconnect delayed)";
    diag("pre-check2");
    master.check_grace_deadlines_for_testing(9999999999LL);
    diag("post-check2");
    failed = master.get_failed_tasks();
    dep_D_failed = false;
    for (auto id : failed) { if (id == 50) dep_D_failed = true; }
    EXPECT_TRUE(dep_D_failed) << "After all holders dead, dependent waiting task must fast-fail";

    master.stop();
    wait_for_running(master, false);
    Logger::shutdown();
}

// --- register_database + is_db_frozen ---

TEST(MasterAgentTest, RegisterDatabaseAndIsFrozen) {
    fly::DataService::instance()->reset();
    TempDir tmpdir;
    CMString db_path = db32("test_db_reg_freeze");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");

    // Not frozen initially
    EXPECT_FALSE(master.is_db_frozen(db_path));

    // Unknown db is also not frozen
    EXPECT_FALSE(master.is_db_frozen(db32("unknown_db_freeze")));

    // Freeze via network: start master, connect worker, send freeze request
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_path);

    // Wait for master to process the freeze
    wait_for([&] { return master.is_db_frozen(db_path); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_path));

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

// =============================================================================
// Non-stream mode: pending frozen state machine (WP1)
// 非 stream 模式 = task 级原子性：freeze 在 task 内声明为 pending，
// task 成功才迁移到 confirmed + 广播；task 失败/崩溃则按 task_id 回滚 pending。
// =============================================================================

// T2: 非 stream 模式下 worker 在 task 内 freeze → master 登记为 pending →
//     is_db_frozen 覆盖 pending（跨 task 写注册拦截）；task 成功后迁移到 confirmed。
TEST(MasterAgentTest, NonStreamFreezePendingThenCommit) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);   // 非 stream 模式
    TempDir tmpdir;
    CMString db_path = db32("nstream_freeze_commit");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 模拟 task 内 freeze：begin_task 设 current_task_id_，然后 freeze
    worker.begin_task(1001, "");
    worker.request_database_freeze(db_path);

    // pending 登记：is_db_frozen 应覆盖 pending（跨 task 写注册需被拦截）
    wait_for([&] { return master.is_db_frozen(db_path); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_path));          // confirmed ∪ pending
    EXPECT_TRUE(master.is_db_pending_frozen(db_path));  // 仅 pending（未提交）

    // 模拟 task 成功：commit_pending_frozen 把 pending 迁移到 confirmed
    master.commit_pending_frozen(1001);
    EXPECT_TRUE(master.is_db_frozen(db_path));           // 仍是 frozen
    EXPECT_FALSE(master.is_db_pending_frozen(db_path));  // 但不再是 pending（已 confirmed）

    worker.end_task(1001);
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);   // 恢复默认
}

// T4a: 非 stream 模式 task 失败 → rollback_pending_frozen 按 task_id 清 pending。
TEST(MasterAgentTest, NonStreamFreezeRollbackOnTaskFailed) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);
    TempDir tmpdir;
    CMString db_path = db32("nstream_freeze_rollback");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.begin_task(2002, "");
    worker.request_database_freeze(db_path);
    wait_for([&] { return master.is_db_pending_frozen(db_path); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_path));   // pending 状态下也算 frozen

    // 模拟 task 失败：rollback_pending_frozen 清掉该 task 的 pending
    master.rollback_pending_frozen(2002);
    EXPECT_FALSE(master.is_db_frozen(db_path));          // 回滚后不再 frozen
    EXPECT_FALSE(master.is_db_pending_frozen(db_path));  // pending 也清了

    worker.end_task(2002);
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);
}

// T4b: 非 stream 模式 task 成功只提交自己的 pending —— 另一个 task 的 pending 不受影响。
TEST(MasterAgentTest, NonStreamCommitDoesNotAffectOtherTaskPending) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);
    TempDir tmpdir_a, tmpdir_b;
    CMString db_a = tmpdir_a.path();
    CMString db_b = tmpdir_b.path();

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_a, "");
    master.register_database(db_b, "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // task 3001 freeze db_a
    worker.begin_task(3001, "");
    worker.request_database_freeze(db_a);
    // task 3002 freeze db_b
    worker.begin_task(3002, "");
    worker.request_database_freeze(db_b);
    wait_for([&] { return master.is_db_pending_frozen(db_a) && master.is_db_pending_frozen(db_b); }, 50, 20);

    // task 3001 成功：只提交 db_a，db_b 仍是 pending
    master.commit_pending_frozen(3001);
    EXPECT_FALSE(master.is_db_pending_frozen(db_a));   // a 已 confirmed
    EXPECT_TRUE(master.is_db_frozen(db_a));
    EXPECT_TRUE(master.is_db_pending_frozen(db_b));    // b 仍 pending
    EXPECT_TRUE(master.is_db_frozen(db_b));            // 但 b 仍算 frozen（pending）

    worker.end_task(3002);
    worker.end_task(3001);
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);
}

// T3: 冲突 fail-fast —— 对已 (pending) frozen 的 db 再次 freeze → master 拒绝 →
//     worker 收到 DB_ALREADY_FROZEN ack → WorkerAgentContext 记录错误类型。
TEST(MasterAgentTest, NonStreamFreezeConflictRejected) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);
    TempDir tmpdir;
    CMString db_path = db32("nstream_freeze_conflict");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 第一次 freeze 成功（pending）
    worker.begin_task(4001, "");
    worker.request_database_freeze(db_path);
    wait_for([&] { return master.is_db_pending_frozen(db_path); }, 50, 20);

    // 第二次 freeze 同一 db（模拟另一 task 业务流程错误）→ 应被拒绝
    worker.begin_task(4002, "");
    worker.request_database_freeze(db_path);

    // worker 收到 DB_ALREADY_FROZEN ack → last_error_type 被设置
    wait_for([&] {
        return WorkerAgentContext::get_last_error_type() == TaskErrorType::DB_ALREADY_FROZEN;
    }, 50, 20);
    EXPECT_EQ(WorkerAgentContext::get_last_error_type(), TaskErrorType::DB_ALREADY_FROZEN);

    worker.end_task(4002);
    worker.end_task(4001);
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);
}

// T5: 崩溃恢复 —— worker 有正在跑的 task 且该 task 声明了 pending freeze →
//     worker 断连（模拟崩溃）→ master on_disconnect 按 task_id 清 pending（防死锁）。
//     这是 Q1 选 task_id 而非 db_path 的核心理由：崩溃时 master 收不到失败消息。
TEST(MasterAgentTest, NonStreamFreezeClearedOnWorkerCrash) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);
    TempDir tmpdir;
    CMString db_path = tmpdir.path();

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 提交一个 task（让它在 worker 上 RUNNING，这样断连时 master 能反查到 task_id）
    master.submit_task(5005, "crash_task", "test_module", {"arg1"}, {}, {});
    wait_for([&] {
        auto r = master.get_running_tasks();
        for (auto id : r) { if (id == 5005) return true; }
        return false;
    }, 50, 20);

    // 该 task 在执行中声明了 pending freeze
    worker.begin_task(5005, "");
    worker.request_database_freeze(db_path);
    wait_for([&] { return master.is_db_pending_frozen(db_path); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_path));   // pending → 写注册被拦截

    // 模拟 worker 崩溃：stop() 触发断连
    worker.stop();

    // 宽限语义（用户确认）：断连期 pending 保留（worker 可能重连恢复执行）；
    // 宽限超时判死 → handle_worker_death 按 task_id 清 pending（防永久死锁）。
    wait_for([&]{ return master.get_connected_workers().empty(); }, 100, 30);
    master.check_grace_deadlines_for_testing(9999999999LL);

    // 判死后 pending 应被清掉
    wait_for([&] { return !master.is_db_frozen(db_path); }, 100, 30);
    EXPECT_FALSE(master.is_db_frozen(db_path));          // pending 已清
    EXPECT_FALSE(master.is_db_pending_frozen(db_path));  // 不再残留

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);
}

// =============================================================================
// Non-stream mode: write register visibility delay (WP2)
// 非 stream 模式 = task 级原子性：write register 即时校验（provenance + frozen），
// 但可见性登记（mark_data_ready + update_remote_idx）延迟到 task 成功完成。
// 这保证 task 失败回滚后，下游 task 不会被错误调度。
// =============================================================================

// WP2-T1: 非 stream 模式下 worker write register 成功（ack success），但下游依赖 task
//         不会立即 ready（mark_data_ready 延迟）；task complete 后才 ready。
TEST(MasterAgentTest, NonStreamWriteRegisterDelaysDataReady) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);   // 非 stream 模式
    TempDir tmpdir;
    CMString db_path = db32("nswr");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_x = db_path + ":obj_x";

    // 先提交 task 7000（产出 obj_x）→ 它 ready 并被调度到 worker（running）
    master.submit_task(7000, "producer", "test_module", {"arg"},
                       {}, {obj_x}, {}, -1.0f, "", {});
    wait_for([&] {
        auto running = master.get_running_tasks();
        return std::find(running.begin(), running.end(), 7000) != running.end();
    }, 50, 20);

    // 再提交依赖 obj_x 的 task 7001 → 因 obj_x 未 ready（7000 未完成），7001 进 pending
    master.submit_task(7001, "consumer", "test_module", {"arg"},
                       {obj_x}, {}, {}, -1.0f, "", {});
    {
        auto pending = master.get_pending_tasks();
        EXPECT_NE(std::find(pending.begin(), pending.end(), 7001), pending.end());
    }

    // worker 在 task 7000 内 write obj_x（注册成功，但非 stream 模式不 mark_data_ready）
    worker.begin_task(7000, "");
    auto [ack_msg, err_type] = worker.register_write_with_master(db_path, "obj_x", 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);   // 校验通过，ack 成功

    // 关键断言：write 后 7001 仍在 pending（mark_data_ready 被延迟）
    wait_for([&] { return true; }, 5, 20);
    {
        auto pending = master.get_pending_tasks();
        EXPECT_NE(std::find(pending.begin(), pending.end(), 7001), pending.end());
    }

    // 模拟 task 7000 完成：发 TaskCompleteMessage（含 written_objects obj_x）
    TaskCompleteMessage complete;
    complete.task_id_ = 7000;
    complete.worker_id_ = 1;
    WrittenObject wo;
    wo.object_name_ = obj_x;
    wo.size_bytes_ = 100;
    complete.written_objects_.push_back(wo);
    master.on_task_complete(0, complete);

    // task complete 后 mark_data_ready 触发 → 7001 移出 pending（可被调度）
    wait_for([&] {
        auto pending = master.get_pending_tasks();
        return std::find(pending.begin(), pending.end(), 7001) == pending.end();
    }, 50, 20);
    {
        auto pending = master.get_pending_tasks();
        EXPECT_EQ(std::find(pending.begin(), pending.end(), 7001), pending.end());
    }

    worker.end_task(7000);
    // 7001（consumer）已因 obj_x ready 被 assign 成 RUNNING——stop 前终结
    //（drain 无 deadline，不终结会死等）。
    complete_task_for_stop(master, 7001, 1);
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);
}

// batch（非 stream）模式下，TaskComplete 的 written_objects_.size_bytes_ 是
// remote_idx 的唯一 size 来源（do_write_register 在 batch 模式跳过 update_remote_idx）。
// 本测试驱动真实 worker 的 size 携带链路（record_write→end_task→TaskComplete），
// 验证 master remote_idx 记录的是真实 size 而非 0。
// 回归保护：原 current_write_sizes_ 时序 bug（end_task 先 clear map，TaskComplete
// 构造时查 size 恒得 0）导致 batch 模式对象 size 永久为 0 → locality 打分失准。
TEST(MasterAgentTest, NonStreamTaskCompleteCarriesRealSize) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);   // batch 模式
    TempDir tmpdir;
    CMString db_path = db32("nssz");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_a = db_path + ":obj_a";

    // worker 在 task 内写对象：register（向 master 校验/provenance）+ record（本地记录写出）。
    // 真实流程里两者由 commit_write 落盘完成回调同时触发；测试显式调用以驱动 size 链路。
    worker.begin_task(8000, "");
    auto [ack_msg, err_type] = worker.register_write_with_master(db_path, "obj_a", 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);   // batch 模式：校验通过
    worker.record_write(db_path, "obj_a", 100);      // 记录写出（full_name + size）

    // batch 模式下，do_write_register 跳过 update_remote_idx —— 对象登记前 remote_idx 无记录
    EXPECT_FALSE(DataService::instance()->has_remote_location(obj_a));

    // end_task 返回带 size 的 WriteRecord（验证 worker 端 size 携带正确）
    auto writes = worker.end_task(8000);
    ASSERT_EQ(writes.size(), 1u);
    EXPECT_EQ(writes[0].full_name_, obj_a);
    EXPECT_EQ(writes[0].size_bytes_, 100);   // 关键：size 随 WriteRecord 携带，未丢失

    // 用 end_task 返回的 WriteRecord 构造 TaskComplete（模拟 worker 真实上报路径）
    TaskCompleteMessage complete;
    complete.task_id_ = 8000;
    complete.worker_id_ = 1;
    for (const auto& w : writes) {
        complete.written_objects_.push_back({w.full_name_, w.size_bytes_});
    }
    master.on_task_complete(0, complete);

    // batch 模式下 TaskComplete 触发 update_remote_idx —— size 必须是真实值 100
    EXPECT_TRUE(DataService::instance()->has_remote_location(obj_a));
    EXPECT_EQ(DataService::instance()->get_remote_size(obj_a), 100);

    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);
}

// WP2-T2: stream 模式下（默认）write register 即时 mark_data_ready，
//         下游依赖 task 立即 ready（回归保护：stream 行为不变）。
TEST(MasterAgentTest, StreamWriteRegisterImmediateDataReady) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 0);   // stream 模式（默认）
    TempDir tmpdir;
    CMString db_path = db32("swr");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(tmpdir.path(), "");
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_y = db_path + ":obj_y";

    // 先提交 task 7003（产出 obj_y）→ running
    master.submit_task(7003, "producer2", "test_module", {"arg"},
                       {}, {obj_y}, {}, -1.0f, "", {});
    wait_for([&] {
        auto running = master.get_running_tasks();
        return std::find(running.begin(), running.end(), 7003) != running.end();
    }, 50, 20);

    // 提交依赖 obj_y 的 task 7004 → pending
    master.submit_task(7004, "consumer2", "test_module", {"arg"},
                       {obj_y}, {}, {}, -1.0f, "", {});
    {
        auto pending = master.get_pending_tasks();
        EXPECT_NE(std::find(pending.begin(), pending.end(), 7004), pending.end());
    }

    // stream 模式：write register 即时 mark_data_ready
    worker.begin_task(7003, "");
    auto [ack_msg, err_type] = worker.register_write_with_master(db_path, "obj_y", 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);

    // 关键断言：write 后 7004 立即移出 pending（即时 mark_data_ready）
    wait_for([&] {
        auto pending = master.get_pending_tasks();
        return std::find(pending.begin(), pending.end(), 7004) == pending.end();
    }, 50, 20);
    {
        auto pending = master.get_pending_tasks();
        EXPECT_EQ(std::find(pending.begin(), pending.end(), 7004), pending.end());  // 已 ready
    }

    worker.end_task(7003);
    // 7003（producer，master 侧 RUNNING——end_task 只是 worker 本地记录）与
    // 7004（obj_y ready 后被 assign）都要在 stop 前终结（drain 无 deadline）。
    complete_task_for_stop(master, 7003, 1);
    complete_task_for_stop(master, 7004, 1);
    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

// WP2-T3: 非 stream 模式下 task complete 时 record_worker_info 被正确调用（技术债修复）。
//         WrittenObject 带 db_path_ 后，complete 时 master 能补做 record_worker_info，
//         db meta (_DB_META) 应含 worker 写入者记录。
TEST(MasterAgentTest, NonStreamCompleteRecordsWorkerInfo) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);   // 非 stream 模式
    ProcessInfo::instance()->set_hostname("test_host_info");    // record_worker_info 需要 hostname
    TempDir tmpdir;

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // get_or_create_database 真正创建 master 侧 db 对象（写 _DB_META header + 入 db_instances_）
    auto db_obj = master.get_or_create_database(tmpdir.path(), "", 0);
    ASSERT_NE(db_obj, nullptr);
    CMString db_path = db_obj->get_db_path();

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_x = db_path + ":obj_x";

    // 提交产出 obj_x 的 task 让它 running
    master.submit_task(8000, "producer3", "test_module", {"arg"},
                       {}, {obj_x}, {}, -1.0f, "", {});
    wait_for([&] {
        auto running = master.get_running_tasks();
        return std::find(running.begin(), running.end(), 8000) != running.end();
    }, 50, 20);

    worker.begin_task(8000, "");
    worker.register_write_with_master(db_path, "obj_x", 100);

    // task complete（written_objects 带 db_path_）
    TaskCompleteMessage complete;
    complete.task_id_ = 8000;
    complete.worker_id_ = 1;
    WrittenObject wo;
    wo.object_name_ = obj_x;
    wo.size_bytes_ = 100;
    complete.written_objects_.push_back(wo);
    master.on_task_complete(0, complete);

    // master 的 record_worker_info 经注入回调落盘 _DB_META（Python 层
    // DbMetaFile.append_worker；单测未注入回调时仅内存去重）。C++ 侧以
    // recorded_workers 计数验证登记语义。
    EXPECT_GT(master.recorded_workers_count_for_testing(), 0u);   // record_worker_info 生效

    worker.end_task(8000);
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("dependency_update_mode", 0);
}

// --- get_or_create_database ---

TEST(MasterAgentTest, GetOrCreateDatabase) {
    fly::DataService::instance()->reset();
    TempDir tmpdir1;
    TempDir tmpdir2;

    MasterAgent master("127.0.0.1", 0);

    auto db = master.get_or_create_database(tmpdir1.path());
    ASSERT_NE(db, nullptr);

    CMString db_path = db->get_db_path();
    // db_path 废弃：db_path == db_path（不再是固定长度随机串）
    EXPECT_EQ(db_path, tmpdir1.path());
    EXPECT_FALSE(db->get_writer_id().empty());
    EXPECT_EQ(db->get_db_path(), tmpdir1.path());

    auto db2 = master.get_or_create_database(tmpdir2.path());
    ASSERT_NE(db2, nullptr);
    EXPECT_EQ(db2->get_db_path(), tmpdir2.path());
    EXPECT_NE(db->get_db_path(), db2->get_db_path());

    DataService::instance()->unregister_database(db->get_db_path());
    DataService::instance()->unregister_database(db2->get_db_path());
}

// --- get_task_error ---

TEST(MasterAgentTest, GetTaskError) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("fail_unscheduleable_tasks", 1);
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // Submit task with impossible capability requirement
    // fail_unscheduleable_tasks=1 by default → task fails immediately
    master.submit_task(300, "impossible_task", "test_mod", {"arg"}, {}, {}, {"nonexistent_cap_xyz"});

    // Wait for task to fail
    wait_for([&] {
        auto failed = master.get_failed_tasks();
        for (auto id : failed) { if (id == 300) return true; }
        return false;
    }, 50, 20);

    CMString error = master.get_task_error(300);
    EXPECT_FALSE(error.empty());
    EXPECT_NE(error.find("required capabilities"), CMString::npos);

    // Non-existent task returns empty string
    EXPECT_TRUE(master.get_task_error(99999).empty());

    master.stop();
    wait_for_running(master, false);
}

// --- get_idle_workers ---

TEST(MasterAgentTest, GetIdleWorkers) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // No workers connected → empty idle list
    auto idle = master.get_idle_workers();
    EXPECT_TRUE(idle.empty());

    // Connect a worker
    WorkerAgent worker(502, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // Wait for worker to appear in idle list
    wait_for([&] {
        auto iw = master.get_idle_workers();
        for (auto id : iw) { if (id == 502) return true; }
        return false;
    }, 50, 20);

    idle = master.get_idle_workers();
    bool found = false;
    for (auto id : idle) { if (id == 502) { found = true; break; } }
    EXPECT_TRUE(found);

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

// role（F3）：storage_only 注册后连接表含（在线可数据服务）但调度候选不含；
// hybrid 照常进入候选。
TEST(MasterAgentTest, StorageOnlyWorkerNotInIdleCandidates) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent hybrid(601, "127.0.0.1", master.get_port(), {}, "hybrid");
    WorkerAgent storage(602, "127.0.0.1", master.get_port(), {}, "storage_only");
    hybrid.start();
    storage.start();
    ASSERT_TRUE(wait_until_registered(hybrid));
    ASSERT_TRUE(wait_until_registered(storage));

    // 两者都连接（数据面成员）。
    wait_for([&]{
        auto cs = master.get_connected_workers();
        int n = 0;
        for (auto w : cs) { if (w == 601 || w == 602) ++n; }
        return n == 2;
    }, 50, 20);

    // idle 候选只含 hybrid。
    wait_for([&]{
        auto iw = master.get_idle_workers();
        bool has_hybrid = false, has_storage = false;
        for (auto id : iw) {
            if (id == 601) has_hybrid = true;
            if (id == 602) has_storage = true;
        }
        return has_hybrid && !has_storage;
    }, 50, 20);
    auto idle = master.get_idle_workers();
    bool has_storage = false;
    for (auto id : idle) { if (id == 602) has_storage = true; }
    EXPECT_FALSE(has_storage) << "storage_only must not appear in idle candidates";

    hybrid.stop();
    storage.stop();
    master.stop();
    wait_for_running(master, false);
}

TEST(MasterAgentTest, GetConnectedWorkers) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // No connections initially
    EXPECT_TRUE(master.get_connected_workers().empty());

    WorkerAgent worker(503, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    auto connected = master.get_connected_workers();
    bool found = false;
    for (auto id : connected) { if (id == 503) { found = true; break; } }
    EXPECT_TRUE(found);

    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

// --- get_connection_count ---

TEST(MasterAgentTest, GetConnectionCount) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    EXPECT_EQ(master.get_connection_count(), 0u);

    WorkerAgent worker1(510, "127.0.0.1", master.get_port());
    worker1.start();
    ASSERT_TRUE(wait_until_registered(worker1));

    wait_for([&] { return master.get_connection_count() >= 1u; }, 50, 20);
    EXPECT_EQ(master.get_connection_count(), 1u);

    WorkerAgent worker2(511, "127.0.0.1", master.get_port());
    worker2.start();
    ASSERT_TRUE(wait_until_registered(worker2));

    wait_for([&] { return master.get_connection_count() >= 2u; }, 50, 20);
    EXPECT_EQ(master.get_connection_count(), 2u);

    worker1.stop();
    worker2.stop();
    master.stop();
    wait_for_running(master, false);
}

// --- add_worker_hostname + get_worker_hostnames ---

TEST(MasterAgentTest, AddWorkerHostnameAndGetHostnames) {
    MasterAgent master("127.0.0.1", 0);

    // Empty initially
    EXPECT_TRUE(master.get_worker_hostnames().empty());

    master.add_worker_hostname(600, "host_alpha");
    master.add_worker_hostname(601, "host_beta");

    auto hostnames = master.get_worker_hostnames();
    ASSERT_EQ(hostnames.size(), 2u);

    // Build a map for easier lookup
    CMMap<uint64_t, CMString> hostname_map;
    for (const auto& [wid, hname] : hostnames) {
        hostname_map[wid] = hname;
    }

    EXPECT_EQ(hostname_map[600], "host_alpha");
    EXPECT_EQ(hostname_map[601], "host_beta");

    // Overwrite existing hostname
    master.add_worker_hostname(600, "host_gamma");
    hostnames = master.get_worker_hostnames();
    hostname_map.clear();
    for (const auto& [wid, hname] : hostnames) {
        hostname_map[wid] = hname;
    }
    EXPECT_EQ(hostname_map[600], "host_gamma");
    EXPECT_EQ(hostname_map[601], "host_beta");
}

// --- select_backup_worker host 级分散选择 ---

// 副本在 W1@hostA；候选 W2@hostB（host 全新）、W3@hostA（冲突）→ 应选 W2（不选同 host 的 W3）。
TEST(MasterAgentTest, SelectBackupWorkerPrefersHostDisjoint) {
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(20, "host_b");
    master.add_worker_hostname(30, "host_a");

    CMString obj = db32("backup_disjoint") + ":obj";
    DataService::instance()->update_remote_idx(obj, 10, "10.0.0.1", 8001);

    // W2@hostB 是唯一 host 全新的候选；W3@hostA 与 holder 冲突，仅作 fallback。
    EXPECT_EQ(master.select_backup_worker_for_testing(obj), 20u);

    DataService::instance()->remove_remote_index(obj);
}

// 副本在 W1@hostA、W2@hostB（所有 host 都被占）；仅剩 W3@hostA 无副本 → best-effort 回退选 W3。
TEST(MasterAgentTest, SelectBackupWorkerFallbackWhenAllHostsOccupied) {
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(20, "host_b");
    master.add_worker_hostname(30, "host_a");

    CMString obj = db32("backup_fallback") + ":obj";
    DataService::instance()->update_remote_idx(obj, 10, "10.0.0.1", 8001);
    DataService::instance()->update_remote_idx(obj, 20, "10.0.0.2", 8002);

    // hostA、hostB 都已有副本；W3@hostA 是唯一无副本的 worker → best-effort 回退选它。
    EXPECT_EQ(master.select_backup_worker_for_testing(obj), 30u);

    DataService::instance()->remove_remote_index(obj);
}

// 副本在 W1@hostA、W2@hostB；候选 W3@hostC（host 全新）、W4@hostA（冲突）→ 应避开两个 holder host 选 W3。
TEST(MasterAgentTest, SelectBackupWorkerAvoidsAllHolderHosts) {
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(20, "host_b");
    master.add_worker_hostname(30, "host_c");
    master.add_worker_hostname(40, "host_a");

    CMString obj = db32("backup_avoid_all") + ":obj";
    DataService::instance()->update_remote_idx(obj, 10, "10.0.0.1", 8001);
    DataService::instance()->update_remote_idx(obj, 20, "10.0.0.2", 8002);

    // W3@hostC 是唯一 host 全新的候选（避开 hostA、hostB）；W4@hostA 冲突，不应被选。
    EXPECT_EQ(master.select_backup_worker_for_testing(obj), 30u);

    DataService::instance()->remove_remote_index(obj);
}

// host-disjoint 层内 storage_only 优先：副本 W1@hostA；候选 W2@hostB(hybrid)、
// W3@hostC(storage_only) 同为 host 全新层 → storage 胜出。
TEST(MasterAgentTest, SelectBackupWorkerPrefersStorageOnly) {
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(20, "host_b");
    master.add_worker_hostname(30, "host_c", WorkerRole::STORAGE_ONLY);

    CMString obj = db32("backup_pref_storage") + ":obj";
    DataService::instance()->update_remote_idx(obj, 10, "10.0.0.1", 8001);

    EXPECT_EQ(master.select_backup_worker_for_testing(obj), 30u);

    DataService::instance()->remove_remote_index(obj);
}

// host 故障域隔离压过 storage 偏好：副本 W1@hostA；候选 W2@hostB(hybrid, host 全新)
// vs W3@hostA(storage_only, host 冲突仅 fallback) → 必须选 W2（role 不越级）。
TEST(MasterAgentTest, SelectBackupWorkerHostDisjointBeatsStorageRole) {
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(20, "host_b");
    master.add_worker_hostname(30, "host_a", WorkerRole::STORAGE_ONLY);

    CMString obj = db32("backup_disjoint_beats_role") + ":obj";
    DataService::instance()->update_remote_idx(obj, 10, "10.0.0.1", 8001);

    EXPECT_EQ(master.select_backup_worker_for_testing(obj), 20u);

    DataService::instance()->remove_remote_index(obj);
}

// 同为 host-disjoint storage_only 候选时按磁盘水位最轻：W2 名下已持有 1000B
// 副本、W3 为空 → 选 W3。
TEST(MasterAgentTest, SelectBackupWorkerPrefersLighterStorage) {
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(20, "host_b", WorkerRole::STORAGE_ONLY);
    master.add_worker_hostname(30, "host_c", WorkerRole::STORAGE_ONLY);

    CMString obj = db32("backup_light_storage") + ":obj";
    DataService::instance()->update_remote_idx(obj, 10, "10.0.0.1", 8001);
    // W2 的存量水位（另一个对象，size=1000）。
    CMString old_obj = db32("backup_light_storage") + ":old";
    DataService::instance()->update_remote_idx(old_obj, 20, "10.0.0.2", 8002, 1000);

    EXPECT_EQ(master.select_backup_worker_for_testing(obj), 30u);

    DataService::instance()->remove_remote_index(obj);
    DataService::instance()->remove_remote_index(old_obj);
}

// --- auto_backup EWMA 聚合判定（2026-08-16 补覆盖：此前 master 侧判定零测试）---

namespace {
// EWMA 用例公共环境：低阈值 + 关闭 bytes 分数 + 恢复默认。
// decay=0（不衰减）供确定性用例；衰减用例自行覆盖。
struct AutoBackupEwmaFixture {
    CMSharedPtr<Config> cfg = Config::instance();
    std::vector<std::pair<const char*, int64_t>> saved;
    AutoBackupEwmaFixture() {
        for (const char* k : {"master_ewma_decay_per_sec", "backup_count_threshold",
                              "backup_bytes_threshold", "max_backup_replicas",
                              "backup_large_object_threshold", "backup_high_score_threshold"}) {
            saved.emplace_back(k, cfg->get_int(k));
        }
        cfg->set_int("master_ewma_decay_per_sec", 0);   // 确定性：默认用例不衰减
        cfg->set_int("backup_count_threshold", 2);
        cfg->set_int("backup_bytes_threshold", 0);      // 关闭 bytes 分数，单测 count 维度
        cfg->set_int("max_backup_replicas", 2);
        cfg->set_int("backup_large_object_threshold", 1 << 30);
        cfg->set_int("backup_high_score_threshold", 1 << 30);
    }
    ~AutoBackupEwmaFixture() {
        for (auto& [k, v] : saved) cfg->set_int(k, v);
    }
    static WorkerBackupSuggestMessage make_suggest(const CMString& obj, uint64_t delta_count,
                                                   uint64_t delta_bytes = 0, int64_t size_bytes = 100) {
        WorkerBackupSuggestMessage msg;
        msg.object_name_ = obj;
        msg.delta_count_ = delta_count;
        msg.delta_bytes_ = delta_bytes;
        msg.size_bytes_ = size_bytes;
        return msg;
    }
};
}  // namespace

TEST(MasterAgentTest, AutoBackupEwmaAccumulatesAndTriggersAtThreshold) {
    AutoBackupEwmaFixture fx;
    MasterAgent master("127.0.0.1", 0);

    CMString obj = db32("ewma_trigger") + ":obj";
    DataService::instance()->update_remote_idx(obj, 1, "10.0.0.1", 8001);

    // 两次 suggest delta_count=1、3 → 首次 score=1 < 2 不触发；二次 cumulative=4，
    // score=4/1 replica=4 >= 2 → 恰好触发 1 次。（delta_count_ 为 uint64 整型。）
    master.worker_backup_suggest_for_testing(AutoBackupEwmaFixture::make_suggest(obj, 1));
    master.worker_backup_suggest_for_testing(AutoBackupEwmaFixture::make_suggest(obj, 3));

    auto score = master.backup_score_for_testing(obj);
    EXPECT_DOUBLE_EQ(score.cumulative_count_, 4.0);
    EXPECT_EQ(score.size_bytes_, 100);
    EXPECT_EQ(master.auto_backup_trigger_count_for_testing_, 1u);

    DataService::instance()->remove_remote_index(obj);
}

TEST(MasterAgentTest, AutoBackupEwmaDecaysOverTime) {
    AutoBackupEwmaFixture fx;
    Config::instance()->set_int("master_ewma_decay_per_sec", 30);  // 30%/s
    MasterAgent master("127.0.0.1", 0);

    CMString obj = db32("ewma_decay") + ":obj";
    DataService::instance()->update_remote_idx(obj, 1, "10.0.0.1", 8001);

    master.worker_backup_suggest_for_testing(AutoBackupEwmaFixture::make_suggest(obj, 10.0));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    master.worker_backup_suggest_for_testing(AutoBackupEwmaFixture::make_suggest(obj, 10.0));

    // 2s × 30%/s → factor = 0.7² = 0.49；cumulative ≈ 10×0.49 + 10 = 14.9。
    // 衰减区间断言（避开精确时钟，覆盖 (13, 16) 即证实衰减生效且未误伤累积）。
    auto score = master.backup_score_for_testing(obj);
    EXPECT_GT(score.cumulative_count_, 13.0);
    EXPECT_LT(score.cumulative_count_, 16.0);

    DataService::instance()->remove_remote_index(obj);
}

TEST(MasterAgentTest, AutoBackupEwmaBelowThresholdNoTrigger) {
    AutoBackupEwmaFixture fx;
    MasterAgent master("127.0.0.1", 0);

    CMString obj = db32("ewma_below") + ":obj";
    DataService::instance()->update_remote_idx(obj, 1, "10.0.0.1", 8001);

    master.worker_backup_suggest_for_testing(AutoBackupEwmaFixture::make_suggest(obj, 1.0));

    auto score = master.backup_score_for_testing(obj);
    EXPECT_DOUBLE_EQ(score.cumulative_count_, 1.0);   // 累积生效
    EXPECT_EQ(master.auto_backup_trigger_count_for_testing_, 0u);  // 但 1 < 2 未达阈值

    DataService::instance()->remove_remote_index(obj);
}

TEST(MasterAgentTest, AutoBackupEwmaReplicaCapBlocksTrigger) {
    AutoBackupEwmaFixture fx;
    MasterAgent master("127.0.0.1", 0);

    CMString obj = db32("ewma_cap") + ":obj";
    DataService::instance()->update_remote_idx(obj, 1, "10.0.0.1", 8001);
    DataService::instance()->update_remote_idx(obj, 2, "10.0.0.2", 8002);
    // 2 holders == max_backup_replicas(2) → 分数再高也到 cap，不触发。

    master.worker_backup_suggest_for_testing(AutoBackupEwmaFixture::make_suggest(obj, 100.0));

    auto score = master.backup_score_for_testing(obj);
    EXPECT_DOUBLE_EQ(score.cumulative_count_, 100.0);
    EXPECT_EQ(master.auto_backup_trigger_count_for_testing_, 0u);

    DataService::instance()->remove_remote_index(obj);
}

TEST(MasterAgentTest, AutoBackupEwmaLargeObjectExceptionRaisesCap) {
    AutoBackupEwmaFixture fx;
    Config::instance()->set_int("backup_large_object_threshold", 1000);
    Config::instance()->set_int("backup_high_score_threshold", 50);
    Config::instance()->set_int("backup_extra_slots", 1);
    MasterAgent master("127.0.0.1", 0);

    CMString obj = db32("ewma_large") + ":obj";
    DataService::instance()->update_remote_idx(obj, 1, "10.0.0.1", 8001);
    DataService::instance()->update_remote_idx(obj, 2, "10.0.0.2", 8002);

    // 热点（delta_count=4 / 2 holders → score_count=2 >= 2）+ 大对象（size 2000 >= 1000）
    // + 高 bytes 分数（delta_bytes=200 → score_bytes=100 >= 50）→ cap = max(2) + extra(1) = 3
    // > replicas=2 → 触发。注意豁免判定在热点判定之后，且用的是 score_bytes 非 count。
    master.worker_backup_suggest_for_testing(
        AutoBackupEwmaFixture::make_suggest(obj, /*delta_count=*/4, /*delta_bytes=*/200, /*size_bytes=*/2000));

    EXPECT_EQ(master.auto_backup_trigger_count_for_testing_, 1u);

    DataService::instance()->remove_remote_index(obj);
}

// --- 存储接管决策（storage_takeover）---

namespace {
// 接管用例的公共环境：开启 feature + 同 host 拓扑 + recorded_workers_ 注入。
struct StorageTakeoverFixture {
    CMSharedPtr<Config> cfg = Config::instance();
    int saved_enabled;
    StorageTakeoverFixture() {
        saved_enabled = cfg->get_int("storage_takeover_enabled");
        cfg->set_int("storage_takeover_enabled", 1);
    }
    ~StorageTakeoverFixture() {
        cfg->set_int("storage_takeover_enabled", saved_enabled);
    }
};
}  // namespace

// 同 host 存活 storage_only → 接管发起成功（load 计入 storage worker，pending 登记）。
TEST(MasterAgentTest, StorageTakeoverSelectsSameHostStorage) {
    StorageTakeoverFixture fx;
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");                             // 死 worker
    master.add_worker_hostname(11, "host_a", WorkerRole::STORAGE_ONLY);  // 接管者
    master.add_worker_hostname(12, "host_b", WorkerRole::STORAGE_ONLY);  // 异 host，不选
    master.insert_recorded_worker_for_testing("/db1", "host_a", "wr_dead1");
    master.insert_recorded_worker_for_testing("/db1", "host_a", "wr_dead2");

    EXPECT_TRUE(master.try_storage_takeover_for_testing(10));
    EXPECT_EQ(master.takeover_load_for_testing(11), 2);   // 两个 writer 计入 w11
    EXPECT_EQ(master.takeover_load_for_testing(12), 0);   // 异 host 不计
    EXPECT_EQ(master.takeover_pending_size_for_testing(), 1u);
}

// 同 host 无 storage_only → 不接管（保持现状立即 fail 路径）。
TEST(MasterAgentTest, StorageTakeoverNoStorageOnHost) {
    StorageTakeoverFixture fx;
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(12, "host_b", WorkerRole::STORAGE_ONLY);  // 异 host
    master.insert_recorded_worker_for_testing("/db1", "host_a", "wr_dead1");

    EXPECT_FALSE(master.try_storage_takeover_for_testing(10));
    EXPECT_EQ(master.takeover_pending_size_for_testing(), 0u);
}

// feature 默认关（Config fixture 外）→ 不接管。
TEST(MasterAgentTest, StorageTakeoverDisabledByConfig) {
    MasterAgent master("127.0.0.1", 0);
    Config::instance()->set_int("storage_takeover_enabled", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(11, "host_a", WorkerRole::STORAGE_ONLY);
    master.insert_recorded_worker_for_testing("/db1", "host_a", "wr_dead1");

    EXPECT_FALSE(master.try_storage_takeover_for_testing(10));
}

// writer 数超上限 → 放弃接管（防同 host 连挂涌向单一 storage）。
TEST(MasterAgentTest, StorageTakeoverRespectsMaxWriters) {
    StorageTakeoverFixture fx;
    Config::instance()->set_int("storage_takeover_max_writers", 2);
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(11, "host_a", WorkerRole::STORAGE_ONLY);
    master.insert_recorded_worker_for_testing("/db1", "host_a", "wr1");
    master.insert_recorded_worker_for_testing("/db1", "host_a", "wr2");
    master.insert_recorded_worker_for_testing("/db2", "host_a", "wr3");  // 3 > 2

    EXPECT_FALSE(master.try_storage_takeover_for_testing(10));
    EXPECT_EQ(master.takeover_pending_size_for_testing(), 0u);
}

// deadline 到点 → pending 清除（接管在途的兜底路径；fail_orphan 幂等重判在
// 完整 QA 流程覆盖，这里验证状态机收敛不悬挂）。
TEST(MasterAgentTest, StorageTakeoverDeadlineClearsPending) {
    StorageTakeoverFixture fx;
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");
    master.add_worker_hostname(11, "host_a", WorkerRole::STORAGE_ONLY);
    master.insert_recorded_worker_for_testing("/db1", "host_a", "wr1");

    ASSERT_TRUE(master.try_storage_takeover_for_testing(10));
    EXPECT_EQ(master.takeover_pending_size_for_testing(), 1u);

    // far-future deadline：直接驱动到点（now >= deadline）。
    master.check_takeover_deadlines_for_testing(INT64_MAX);
    EXPECT_EQ(master.takeover_pending_size_for_testing(), 0u);
}

// --- 自动补齐检测决策（auto_storage_nodes）---

namespace {
struct AutoStorageFixture {
    CMSharedPtr<Config> cfg = Config::instance();
    int saved_enabled;
    AutoStorageFixture() {
        saved_enabled = cfg->get_int("auto_storage_nodes_enabled");
        cfg->set_int("auto_storage_nodes_enabled", 1);
    }
    ~AutoStorageFixture() {
        cfg->set_int("auto_storage_nodes_enabled", saved_enabled);
    }
};
}  // namespace

// 缺 storage 的 host 触发 spawn 决策；已有 storage 的 host 不触发。
// 决策计数与发送成败解耦（单测无网络连接，send 失败不登记占位、下轮
// 允许重试——worker 不在线时重试正是期望行为；占位防重、发送与注册
// 链路由 QA test_auto_storage_spawn 端到端覆盖）。
TEST(MasterAgentTest, AutoStorageSpawnSelectsMissingHost) {
    AutoStorageFixture fx;
    MasterAgent master("127.0.0.1", 0);
    master.add_worker_hostname(10, "host_a");                              // 缺 storage
    master.add_worker_hostname(20, "host_b");                              // 缺 storage
    master.add_worker_hostname(30, "host_b", WorkerRole::STORAGE_ONLY);    // host_b 已覆盖

    master.check_storage_nodes_for_testing(1000);
    EXPECT_EQ(master.storage_spawn_decisions_for_testing(), 1);  // 仅 host_a（host_b 被覆盖跳过）
}

// feature 关闭时不触发。
TEST(MasterAgentTest, AutoStorageSpawnDisabledByConfig) {
    MasterAgent master("127.0.0.1", 0);
    Config::instance()->set_int("auto_storage_nodes_enabled", 0);
    master.add_worker_hostname(10, "host_a");

    master.check_storage_nodes_for_testing(1000);
    EXPECT_EQ(master.storage_spawn_decisions_for_testing(), 0);
}

// --- Shutdown / Drain tests ---

namespace {

CMVector<FailedTaskRecord> read_failed_records(const CMString& file_path) {
    CMVector<FailedTaskRecord> result;
    if (!std::filesystem::exists(file_path)) return result;
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs) return result;
    while (true) {
        int64_t body_size = 0;
        ifs.read(reinterpret_cast<char*>(&body_size), sizeof(body_size));
        if (!ifs || body_size <= 0) break;
        CMString body(body_size, '\0');
        ifs.read(body.data(), body_size);
        if (!ifs) break;
        FailedTaskRecord record;
        try {
            FLY_DECODE(body, FailedTaskRecord, record);
            result.push_back(std::move(record));
        } catch (...) {}
    }
    return result;
}

}  // anonymous namespace

TEST(MasterAgentTest, StopWithPendingTasks_PersistsThem) {
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    master.submit_task(4001, "pending_task_a", "test_mod", {"arg1"}, {"nonexistent_input_a"}, {});
    master.submit_task(4002, "pending_task_b", "test_mod", {"arg2"}, {"nonexistent_input_b"}, {});

    wait_for([&] {
        auto failed = master.get_failed_tasks();
        auto pending = master.get_pending_tasks();
        size_t total = 0;
        for (auto id : failed) { if (id == 4001 || id == 4002) total++; }
        for (auto id : pending) { if (id == 4001 || id == 4002) total++; }
        return total >= 2;
    }, 50, 20);

    master.stop();
    wait_for_running(master, false);

    CMString file_path = tmpdir.path() + "/failed_tasks.bin";
    auto records = read_failed_records(file_path);
    bool found_4001 = false, found_4002 = false;
    for (const auto& r : records) {
        if (r.task_id_ == 4001) found_4001 = true;
        if (r.task_id_ == 4002) found_4002 = true;
    }
    EXPECT_TRUE(found_4001) << "Task 4001 should be persisted";
    EXPECT_TRUE(found_4002) << "Task 4002 should be persisted";
    EXPECT_GE(records.size(), 2u);

    Config::instance()->set_str("log_dir", "");
}

TEST(MasterAgentTest, StopWithNoRunningTasks_DoesNotBlock) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // No workers, no tasks — stop should complete quickly
    auto start = std::chrono::steady_clock::now();
    master.stop();
    wait_for_running(master, false);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 1000);
}

TEST(MasterAgentTest, StopIsIdempotent) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // First stop
    master.stop();
    wait_for_running(master, false);
    EXPECT_FALSE(master.is_running());

    // Second stop — should not deadlock or crash
    EXPECT_NO_THROW(master.stop());
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, StopBeforeStart_CallsDoDrainAndStop) {
    // Create MasterAgent without start(), call stop() — should not crash
    MasterAgent master("127.0.0.1", 0);
    EXPECT_NO_THROW(master.stop());
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, SubmitTaskCreatesMetadataAndGraph) {
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    master.submit_task(500, "test_submit", "test_module", {"arg1"}, {"missing_input_500"}, {});

    wait_for([&] {
        auto pending = master.get_pending_tasks();
        auto failed = master.get_failed_tasks();
        for (auto id : pending) { if (id == 500) return true; }
        for (auto id : failed) { if (id == 500) return true; }
        return false;
    }, 50, 20);

    auto failed = master.get_failed_tasks();
    auto pending = master.get_pending_tasks();
    bool found = false;
    for (auto id : pending) { if (id == 500) { found = true; break; } }
    for (auto id : failed) { if (id == 500) { found = true; break; } }
    EXPECT_TRUE(found);

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", "");
}

TEST(MasterAgentTest, GetPendingTasksReturnsCorrectIds) {
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());
    Config::instance()->set_int("fail_unscheduleable_tasks", 1);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    master.submit_task(601, "pending1", "mod", {}, {"missing_input_1"}, {});
    master.submit_task(602, "pending2", "mod", {}, {"missing_input_2"}, {});

    wait_for([&] {
        auto failed = master.get_failed_tasks();
        size_t count = 0;
        for (auto id : failed) { if (id == 601 || id == 602) count++; }
        return count >= 2;
    }, 50, 20);

    auto failed = master.get_failed_tasks();
    bool found_601 = false, found_602 = false;
    for (auto id : failed) {
        if (id == 601) found_601 = true;
        if (id == 602) found_602 = true;
    }
    EXPECT_TRUE(found_601);
    EXPECT_TRUE(found_602);

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", "");
}

TEST(MasterAgentTest, GetCompletedTasksInitiallyEmpty) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    auto completed = master.get_completed_tasks();
    EXPECT_TRUE(completed.empty());

    master.stop();
    wait_for_running(master, false);
}

TEST(MasterAgentTest, GetFailedTasksForImpossibleCapabilities) {
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    master.submit_task(700, "impossible", "mod", {"arg"}, {}, {}, {"nonexistent_cap"});

    wait_for([&] {
        auto failed = master.get_failed_tasks();
        for (auto id : failed) { if (id == 700) return true; }
        return false;
    }, 50, 20);

    auto failed = master.get_failed_tasks();
    bool found = false;
    for (auto id : failed) { if (id == 700) { found = true; break; } }
    EXPECT_TRUE(found);

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", "");
}

TEST(MasterAgentTest, GetTaskErrorForNonExistentReturnsEmpty) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    CMString error = master.get_task_error(99999);
    EXPECT_TRUE(error.empty());

    master.stop();
    wait_for_running(master, false);
}

TEST(MasterAgentTest, RegisterDatabaseStoresPathInfo) {
    MasterAgent master("127.0.0.1", 0);
    CMString db_path = db32("reg_test");
    CMString base = "/tmp/test_base_" + std::to_string(::getpid());

    master.register_database(base, base + "/data");
    EXPECT_FALSE(master.is_db_frozen(db_path));
}

TEST(MasterAgentTest, GetWorkerHostnamesEmpty) {
    MasterAgent master("127.0.0.1", 0);
    EXPECT_TRUE(master.get_worker_hostnames().empty());
}

TEST(MasterAgentTest, GetIdleWorkersNoWorkersConnected) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    auto idle = master.get_idle_workers();
    EXPECT_TRUE(idle.empty());

    master.stop();
    wait_for_running(master, false);
}

TEST(MasterAgentTest, GetConnectedWorkersNoWorkers) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    EXPECT_TRUE(master.get_connected_workers().empty());
    EXPECT_EQ(master.get_connection_count(), 0u);

    master.stop();
    wait_for_running(master, false);
}

TEST(MasterAgentTest, SubmitTaskWithWriteContextHash) {
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    master.submit_task(800, "ctx_task", "mod", {"arg"}, {"missing_input_ctx"}, {}, {}, -1.0f, "hash123");

    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", "");
}

TEST(MasterAgentTest, OnMasterRegisterWriteNotRunning) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.setup_write_context();

    CMString db_path = db32("reg_norun");
    auto [msg, err_type] = WorkerAgentContext::register_write(db_path, "test_obj", 100);

    WorkerAgentContext::clear();
}


// size==0 时 update_remote_idx 不覆盖已记录的 size（防御 rebuild 等无 size 路径清零）。
TEST(DataServiceLocalityTest, UpdateRemoteIdxSizeZeroPreservesExistingSize) {
    auto ds = DataService::instance();
    CMString obj = "db_size0:obj";

    ds->update_remote_idx(obj, 1, "127.0.0.1", 9001, 5000);
    EXPECT_EQ(ds->get_remote_size(obj), 5000);

    // 再次 update（如 on_task_complete 的冗余路径或 rebuild）传 size=0，应保持原值。
    ds->update_remote_idx(obj, 2, "127.0.0.1", 9002, 0);
    EXPECT_EQ(ds->get_remote_size(obj), 5000);
    auto holders = ds->get_remote_workers(obj);
    EXPECT_EQ(holders.size(), 2u);

    ds->remove_remote_index(obj);
}

// backup 副本 size 幂等：同一对象的多个副本登记的 size 一致。
TEST(DataServiceLocalityTest, BackupReplicaSizeIdempotent) {
    auto ds = DataService::instance();
    CMString obj = "db_bkup:obj";

    ds->update_remote_idx(obj, 1, "127.0.0.1", 9001, 8000);
    EXPECT_EQ(ds->get_remote_size(obj), 8000);

    // backup 副本（worker 2）size 应等于原 size。
    ds->update_remote_idx(obj, 2, "127.0.0.1", 9002, 8000);
    EXPECT_EQ(ds->get_remote_size(obj), 8000);

    // 再次 backup（worker 3）传 size=0（internal backup task 路径），size 仍保持原值。
    ds->update_remote_idx(obj, 3, "127.0.0.1", 9003, 0);
    EXPECT_EQ(ds->get_remote_size(obj), 8000);

    auto holders = ds->get_remote_workers(obj);
    EXPECT_EQ(holders.size(), 3u);

    ds->remove_remote_index(obj);
}

// master 自写对象（worker_id==0）+ auto_backup_enabled 时，do_write_register 应正确执行
// backup 评估分支（迁移自原 on_data_ready 的 worker_id==0 路径）。
// 验证：① 不崩溃；② 对象正确登记 placement + size；③ backup 评估路径被走通（即使单 master 无 backup worker）。
TEST(MasterAgentTest, MasterSelfWriteWithAutoBackupEnabled) {
    WorkerAgentContext::clear();
    Config::instance()->set_int("auto_backup_enabled", 1);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    CMString db_path = db32("test_self_bk");
    CMString obj_name = "self_bk_obj";
    CMString full_name = db_path + ":" + obj_name;

    // master 自写，auto_backup_enabled=1 → do_write_register 进入 evaluate_and_trigger_backup 分支。
    // 单 master 无其它 worker 做 backup 目标，trigger_auto_backup 会 no-op，但路径必须走通不崩溃。
    auto [msg, err_type] = WorkerAgentContext::register_write(db_path, obj_name, 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);

    // placement + size 正确登记（backup 评估的前提）。
    EXPECT_TRUE(DataService::instance()->has_remote_location(full_name));
    auto info = DataService::instance()->lookup_remote_idx(full_name);
    EXPECT_EQ(info.worker_id_, 0u);
    EXPECT_EQ(DataService::instance()->get_remote_size(full_name), 100);

    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();

    // 恢复默认配置（避免污染其它测试）。
    Config::instance()->set_int("auto_backup_enabled", 0);

    DataService::instance()->remove_remote_index(full_name);
}

// =============================================================================
// issue 007 — Problem 1（高）：assign_task_to_worker 发送/赋值乱序 + scheduler 预占
//
// on_task_complete 的 complete_task(worker_id) 原在 schedule_mutex_ 之外（993 行），
// 可与 scheduler 线程 assign_task_to_worker 内的 worker_manager_->assign_task 交错：
// scheduler 先于 send 处经 task_scheduler.cpp:68 把 worker 设 BUSY；当 reactor 线程
// 极快完成 task 时，complete_task 设 IDLE 后，scheduler 紧接的冗余 assign_task
// （master_agent.cpp:763）覆盖回 BUSY → worker 永久卡 BUSY，current_task_id 指向已完成 task。
// 现有 [COMPLETED-MISMATCH] 诊断只比对 graph↔metadata，检测不到此 worker_manager 分叉。
//
// 确定性复现：两个 std::latch 强制 reactor 线程的 complete_task 落在 scheduler 线程的
// worker_manager_->assign_task 之前（模拟 worker 极快完成的交错）。
//   修复前：worker 终态 BUSY(current=task)  → EXPECT_TRUE(IDLE) 失败（Red）。
//   修复后：worker 终态 IDLE                → 通过（Green）。
// 不 start()：无后台调度/心跳线程，submit_task 末尾同步调 schedule_tasks，消除异步干扰。
// =============================================================================
TEST(MasterAgentTest, Problem1_CompleteTaskClobberedByConcurrentAssign) {
    fly::DataService::instance()->reset();
    TempDir tmpdir;  // on_task_complete→remove_persisted_task 会按 log_dir 建目录，须非空
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();  // start() 内初始化 metadata_/graph_/worker_manager_/scheduler_
    wait_for_running(master, true);

    const uint64_t W = 7001;
    const uint64_t T = 71001;
    const uint64_t fake_conn = 900001;
    master.register_fake_worker_for_testing(W, fake_conn);

    // 两个 latch 协调线程交错：
    //   go_latch   — scheduler 线程（assign 钩子）通知 reactor 线程开始 on_task_complete。
    //   done_latch — reactor 线程（prelock 钩子）通知 scheduler 线程「complete_task 已完成」。
    std::latch go_latch(1), done_latch(1);

    master.assign_task_send_hook_for_testing_ = [&](uint64_t, uint64_t) {
        // scheduler 线程、持 schedule_mutex_、send 之后、762/763 赋值之前。
        go_latch.count_down();   // 通知 reactor 线程开始 on_task_complete
        done_latch.wait();       // 等 reactor 线程完成 complete_task（prelock 信号）
    };
    master.on_task_complete_prelock_hook_for_testing_ = [&](uint64_t, uint64_t) {
        // reactor 线程、获取 schedule_mutex_ 之前；此时 complete_task 已执行（修复前在其上方）。
        done_latch.count_down();  // 通知 scheduler 线程：complete_task 已完成
        // 返回后继续获取 schedule_mutex_（scheduler 持有 → 阻塞，直到 scheduler 释放）
    };

    std::thread reactor_t([&] {
        go_latch.wait();
        TaskCompleteMessage complete;
        complete.task_id_ = T;
        complete.worker_id_ = W;
        complete.is_internal_ = false;
        master.on_task_complete(0, complete);
    });

    // submit_task 同步触发 schedule_tasks → assign_task_to_worker → 钩子协调交错。
    master.submit_task(T, "p1_task", "test_module", {"arg"}, {}, {});

    reactor_t.join();  // 等 on_task_complete 完全结束（含锁内 remove_task/update_status）

    auto idle = master.get_idle_workers();
    bool w_idle = std::find(idle.begin(), idle.end(), W) != idle.end();
    EXPECT_TRUE(w_idle) << "Worker W 应在其 task 完成后回到 IDLE；卡在 BUSY 说明 complete_task "
                        << "被 scheduler 线程的并发 assign_task 覆盖（Problem 1 竞态）";

    master.unregister_fake_worker_for_testing(W, fake_conn);  // 清理 fake 连接（task T 已 complete，stop 无等待项）
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", "");
}

// =============================================================================
// 2026-08-18 solver 3 QA case 事故（test_golden_n50_sd9 等）的回归守护：
// 「依赖不可解检测」不得在「决策→assign 完成」窗口内被并发 schedule_tasks
// 触发误判。
//
// 决策即 graph_->remove_task（task 离开 ready/pending），metadata 的 RUNNING
// 登记在 assign 尾部。若 assign/检测移出 schedule_mutex_ 临界区，并发的
// schedule_tasks 会在窗口内看到「pending 有 consumer + ready 空 + 无 RUNNING
//（producer 已决策未登记）」→ consumer 被整批误判 Unresolvable 失败
//（实测：Task 101010 被误杀，RAS solve 链在 step 101 断裂）。
// 正确性约束 = 决策、assign、检测同在 schedule_mutex_ 临界区（见
// schedule_tasks 锁内注释）。
//
// 确定性交错：assign send 钩子（持锁、send 后、metadata 登记前）有界等待，
// 期间第二线程跑完整 schedule_tasks（含检测）：
//   assign 出锁的错误实现：第二线程不阻塞，检测在窗口内执行 → consumer 被杀；
//   正确实现：第二线程阻塞在 schedule_mutex_，等 assign+登记完成后才进检测。
// =============================================================================
TEST(MasterAgentTest, UnresolvableDetectionDoesNotFireDuringAssignFlight) {
    fly::DataService::instance()->reset();
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    const uint64_t W = 7002;
    const uint64_t PRODUCER = 71002;
    const uint64_t CONSUMER = 71003;
    const uint64_t fake_conn = 900002;
    const CMString out_obj = "db_assign_flight:out";

    // 1) 无 worker 时先提交两 task：producer 停在 ready（无 idle 可派），
    //    consumer pending（其 submit 的同步调度看到 ready 非空，检测不触发）。
    master.submit_task(PRODUCER, "af_producer", "test_module", {"arg"},
                       {}, {out_obj}, {}, -1.0f, "", {});
    master.submit_task(CONSUMER, "af_consumer", "test_module", {"arg"},
                       {out_obj}, {}, {}, -1.0f, "", {});

    // 2) 注册 fake worker（ Problem1 同款）+ 装配 send 钩子。
    master.register_fake_worker_for_testing(W, fake_conn);
    std::latch b_started(1);
    std::mutex done_m;
    std::condition_variable done_cv;
    bool b_finished = false;
    master.assign_task_send_hook_for_testing_ = [&](uint64_t, uint64_t) {
        // 主线程、持 schedule_mutex_、send 之后、metadata 登记之前。
        b_started.count_down();
        // 有界等待第二线程跑完一轮 schedule_tasks（正确实现下第二线程阻塞在
        // schedule_mutex_ 不会完成，300ms 超时让位——两种实现都无死锁）。
        std::unique_lock<std::mutex> lk(done_m);
        done_cv.wait_for(lk, std::chrono::milliseconds(300), [&] { return b_finished; });
    };

    std::thread second([&] {
        b_started.wait();
        master.schedule_tasks();  // 正确实现：阻塞在 schedule_mutex_ 直到 assign+登记完成
        {
            std::lock_guard<std::mutex> lk(done_m);
            b_finished = true;
        }
        done_cv.notify_all();
    });

    // 3) 主线程直接触发调度：决策 producer→W → assign → 钩子协调交错。
    master.schedule_tasks();

    second.join();

    // consumer 不得被误判失败（窗口内「ready 空 + 无 RUNNING」是决策瞬态）。
    auto failed = master.get_failed_tasks();
    EXPECT_EQ(std::find(failed.begin(), failed.end(), CONSUMER), failed.end())
        << "consumer 在 producer 的 assign 窗口内被误判 Unresolvable（2026-08-18 事故）";
    // producer 完成登记：RUNNING。
    auto running = master.get_running_tasks();
    EXPECT_NE(std::find(running.begin(), running.end(), PRODUCER), running.end())
        << "producer 应已完成 assign 并登记 RUNNING";

    master.assign_task_send_hook_for_testing_ = nullptr;
    master.unregister_fake_worker_for_testing(W, fake_conn);
    // PRODUCER assign 给 fake worker 后停留 RUNNING——stop 前终结
    //（drain 无 deadline；fake worker 不会上报 complete）。
    complete_task_for_stop(master, PRODUCER, W);
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", "");
    DataService::instance()->remove_remote_index(out_obj);
}

// =============================================================================
// issue 007 — Problem 5（低）：pending_delete_acks_ / pending_merge_cleanups_ 重复触发
// 静默覆盖。send_delete_data 用 pending_delete_acks_[ack_key] = PendingDeleteData{} 无条件
// 覆盖；若同 ack_key 首轮已完成（ack 已到，completed=true）但尚未被 wait_delete_data_acks
// 消费，二次 send 会把 completed/deleted_count 清零 → ack 不会重发 → wait 永久超时。
// 修复：插入前检查；ack_key 已有 pending 条目则 WARN + 保留旧条目（不重置）。
// =============================================================================
TEST(MasterAgentTest, Problem5_ResendDeleteDataDoesNotResetCompletedAck) {
    fly::DataService::instance()->reset();
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    const uint64_t W = 7005;
    const uint64_t fake_conn = 900005;
    master.register_fake_worker_for_testing(W, fake_conn);

    CMString db = "/diag_p5_db";
    CMString ack_key = db + ":7005";

    // 1) 首次 send_delete_data → 登记 pending（in-progress, completed=false）
    master.send_delete_data(W, db, db + "/data", {"w1"});
    auto s0 = master.pending_delete_ack_state_for_testing(ack_key);
    EXPECT_FALSE(s0.first) << "首轮 send 后应 in-progress";

    // 2) ack 回来 → completed=true, deleted_count=5
    DeleteDataAckMessage ack;
    ack.db_path_ = db;
    ack.worker_id_ = W;
    ack.success_ = true;
    ack.deleted_count_ = 5;
    master.inject_delete_data_ack_for_testing(ack);
    auto s1 = master.pending_delete_ack_state_for_testing(ack_key);
    ASSERT_TRUE(s1.first);
    EXPECT_EQ(s1.second, 5);

    // 3) 同 ack_key 二次 send_delete_data（首轮已完成未被 wait 消费 —— 模拟并发/重复触发）。
    //    Problem 5：修复前 PendingDeleteData{} 覆盖 → {completed=false, deleted_count=0}
    //    （首轮 ack 丢失，wait 会超时）；修复后保留 → {completed=true, deleted_count=5}。
    master.send_delete_data(W, db, db + "/data", {"w1"});
    auto s2 = master.pending_delete_ack_state_for_testing(ack_key);
    EXPECT_TRUE(s2.first) << "二次 send 不应重置已完成的 ack（ack 不会重发，重置致 wait 超时）";
    EXPECT_EQ(s2.second, 5) << "首轮 deleted_count 应保留";

    master.unregister_fake_worker_for_testing(W, fake_conn);
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", "");
}

// Part A: master 自写经 on_master_register_write 取 current_write_hash 登记 provenance。
// 原 on_master_register_write 漏设 write_context_hash_，master 自写走空 hash 旁路无保护。
// 修复后：不同 context hash 写同对象应 mismatch，同 hash 重写幂等。
TEST(MasterAgentTest, MasterSelfWriteRegistersProvenance) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    CMString db_path = db32("prov_self_write");
    CMString obj = "obj";

    // 首次写：task context hash = H1，登记
    WorkerAgentContext::set_current_write_hash("ctx_H1");
    auto [_, type1] = WorkerAgentContext::register_write(db_path, obj, 100);
    EXPECT_EQ(type1, TaskErrorType::UNKNOWN);

    // 不同 hash 写同对象 → mismatch
    WorkerAgentContext::set_current_write_hash("ctx_H2");
    auto [__, type2] = WorkerAgentContext::register_write(db_path, obj, 100);
    EXPECT_EQ(type2, TaskErrorType::WRITE_PROVENANCE_MISMATCH);

    // 同 hash 重写 → 幂等允许
    WorkerAgentContext::set_current_write_hash("ctx_H1");
    auto [___, type3] = WorkerAgentContext::register_write(db_path, obj, 100);
    EXPECT_EQ(type3, TaskErrorType::UNKNOWN);

    WorkerAgentContext::clear_current_write_hash();
    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();
    DataService::instance()->remove_remote_index(db_path + ":" + obj);
}

// 空 write_context_hash 到达 master 是非法注册请求（上游 commit_write 时间戳
// guard / task context 应保证非空）——语义上没有「已有 hash 的对比」，不是
// provenance mismatch。启用 WRITE_REGISTRATION_FAILED（注册被拒的通用兜底）
// 后按其字面语义归类（原实现落入 provenance 空分支误标 MISMATCH）。
TEST(MasterAgentTest, EmptyWriteContextHashRejectedAsRegistrationFailed) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    WriteRegisterMessage msg;
    msg.worker_id_ = 42;
    CMString db_path = db32("empty_hash_reg");
    msg.object_name_ = db_path + ":obj";
    msg.db_path_ = db_path;
    msg.size_bytes_ = 1;
    msg.write_context_hash_ = "";   // 空 hash 直接到达 master（异常路径）

    auto ack = master.do_write_register(msg);
    EXPECT_FALSE(ack.success_);
    EXPECT_EQ(ack.error_type_, TaskErrorType::WRITE_REGISTRATION_FAILED);

    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();
    DataService::instance()->remove_remote_index(db_path + ":obj");
}

// Part B: restore_master_idx 从 idx entry 的 write_context_hash_ 重建 write_provenance_。
// load 后用不同 hash 写同对象 → mismatch（provenance 已从 idx 恢复）。
TEST(MasterAgentTest, RestoreIdxRebuildsProvenance) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    TempDir tmp;
    CMString db_path = tmp.path();

    IndexEntry e;
    e.object_name_ = "obj";
    e.file_name_ = "data.dat";
    e.offset_ = 0;
    e.size_ = 100;
    e.write_context_hash_ = "restored_H1";
    create_idx_file(db_path, "w1", {e});

    auto entries = master.restore_master_idx(db_path, "w1");
    EXPECT_EQ(entries.size(), 1u);

    // 不同 hash 写同对象 → mismatch（provenance 已从 idx 重建）
    WorkerAgentContext::set_current_write_hash("other_H2");
    auto [_, type] = WorkerAgentContext::register_write(db_path, "obj", 100);
    EXPECT_EQ(type, TaskErrorType::WRITE_PROVENANCE_MISMATCH);

    // 同 hash 写 → 幂等允许
    WorkerAgentContext::set_current_write_hash("restored_H1");
    auto [__, type2] = WorkerAgentContext::register_write(db_path, "obj", 100);
    EXPECT_EQ(type2, TaskErrorType::UNKNOWN);

    WorkerAgentContext::clear_current_write_hash();
    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();
    DataService::instance()->remove_remote_index(db_path + ":obj");
}

// Part B frozen 守卫：_FROZEN marker 存在时 restore_master_idx 不重建 provenance。
// 验证：frozen db restore 后写任意 hash 不触发 mismatch（provenance 未重建，首次登记）。
TEST(MasterAgentTest, FrozenDbSkipsProvenanceRebuildOnLoad) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    TempDir tmp;
    CMString db_path = tmp.path();

    IndexEntry e;
    e.object_name_ = "obj";
    e.file_name_ = "data.dat";
    e.offset_ = 0;
    e.size_ = 100;
    e.write_context_hash_ = "frozen_H1";
    create_idx_file(db_path, "w1", {e});
    { std::ofstream f(db_path + "/_FROZEN"); f.put('1'); }  // 磁盘 frozen marker

    master.restore_master_idx(db_path, "w1");

    // frozen 守卫：provenance 未重建，写不同 hash 也是首次登记（UNKNOWN），不是 mismatch。
    // （若守卫失效、provenance 被重建，则 frozen_H1 vs any_hash 会 mismatch。）
    WorkerAgentContext::set_current_write_hash("any_hash");
    auto [_, type] = WorkerAgentContext::register_write(db_path, "obj", 100);
    EXPECT_EQ(type, TaskErrorType::UNKNOWN);

    WorkerAgentContext::clear_current_write_hash();
    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();
    DataService::instance()->remove_remote_index(db_path + ":obj");
}

// Part C: freeze 确认后该 db 的 write_provenance_ 立即清理。
// freeze 后 is_db_frozen 拦下所有写入，provenance 无写入可拦截 → 清理释放内存。
TEST(MasterAgentTest, FreezeClearsProvenance) {
    fly::DataService::instance()->reset();
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    CMString db_path = db32("freeze_prov");

    // master 自写登记 provenance
    WorkerAgentContext::set_current_write_hash("H1");
    WorkerAgentContext::register_write(db_path, "obj", 100);
    EXPECT_EQ(master.provenance_count_for_testing(db_path), 1u);

    // worker 触发 freeze（stream 路径 → cleanup_provenance_for_db）
    WorkerAgent worker(502, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));
    worker.request_database_freeze(db_path);
    wait_for([&] { return master.is_db_frozen(db_path); }, 50, 20);

    EXPECT_EQ(master.provenance_count_for_testing(db_path), 0u) << "freeze 后 provenance 应清理";

    WorkerAgentContext::clear_current_write_hash();
    worker.stop();
    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();
    DataService::instance()->remove_remote_index(db_path + ":obj");
}


// ── SIGTERM 优雅退出 ────────────────────────────────────────────────────────

TEST(GracefulShutdownTest, LampSetResetAndSignalDelivery) {
    fly::reset_graceful_shutdown();
    EXPECT_FALSE(fly::graceful_shutdown_signalled());

    fly::set_graceful_shutdown();
    EXPECT_TRUE(fly::graceful_shutdown_signalled());

    fly::reset_graceful_shutdown();
    EXPECT_FALSE(fly::graceful_shutdown_signalled());

    // 真实信号路径：install 后 raise(SIGTERM)，handler 只置灯（SA_RESTART），
    // 进程不退出、系统调用可重启。
    fly::install_graceful_shutdown_handlers();
    raise(SIGTERM);
    EXPECT_TRUE(fly::graceful_shutdown_signalled());
    fly::reset_graceful_shutdown();
}

TEST(GracefulShutdownTest, TriggerFastExitsMasterWithoutDrain) {
    fly::reset_graceful_shutdown();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true, 100, 20);
    ASSERT_TRUE(master.is_running());

    // SIGTERM 语义 = 快速退出（fast_exit：跳过 drain，无 task 时同样亚秒完成），
    // 在独立线程执行（fast_exit 会 join 本（heartbeat）线程，不能在自身上调用）。
    master.trigger_graceful_shutdown();

    // 幂等：重复触发不再拉起第二个退出线程。
    master.trigger_graceful_shutdown();

    wait_for_running(master, false, 500, 20);
    EXPECT_FALSE(master.is_running());
    // detached fast_exit 线程在 is_running()==false 之后仍有收尾段
    //（RunSummary 落盘 + MetricsDb close，实测 ~100ms）。不等它跑完就返回
    // 的话，析构里的 stop() 因 draining_ 已置而是 no-op——master 对象在
    // detached 线程仍在使用时析构（UAF → terminate，单独重跑本套件 7/10
    // 复现的存量 flake）。可观察的完成信号：MetricsDb 关闭是 stop_impl 中
    // 最后一个触碰 this 的动作。
    wait_for([&] {
        auto* db = master.metrics_db_for_testing();
        return db == nullptr || !db->opened();
    }, 500, 20);
    fly::reset_graceful_shutdown();
}

// 快速退出通道核心语义（用户裁定 2026-08-18）：SIGTERM / graceful_exit（致命
// 错误）不等待 RUNNING task——立即 fail 善后（failed record 持久化）+ 广播
// STOP_NOW（worker 收到即 kill 自身；单测库对象经 hook 拦截观察，不真杀测试
// 进程）。对照正常 stop()：drain 等全部 task 完成（无硬 deadline）。
TEST(MasterAgentTest, FastExitFailsRunningTasksAndStopsWorkers) {
    fly::DataService::instance()->reset();
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    std::mutex mtx;
    std::condition_variable cv;
    bool stop_now_seen = false;
    CMString stop_now_reason;
    worker.stop_now_hook_for_testing_ = [&](const CMString& reason) {
        std::lock_guard<std::mutex> lk(mtx);
        stop_now_seen = true;
        stop_now_reason = reason;
        cv.notify_all();
    };
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // RUNNING task：assign 后永不 complete（无 executor）。
    master.submit_task(6000, "hang_task", "test_module", {"arg"}, {}, {});
    bool task_running = false;
    for (int i = 0; i < 50 && !task_running; ++i) {
        auto running = master.get_running_tasks();
        task_running = std::find(running.begin(), running.end(), 6000) != running.end();
        if (!task_running) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(task_running) << "task 6000 should be assigned and RUNNING";

    auto t0 = std::chrono::steady_clock::now();
    master.fast_exit("test fast exit");
    wait_for_running(master, false);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    // 快速退出上界：fail 善后 + STOP_NOW 广播 + worker 主动断连应亚秒完成；
    // 5s 宽松上界防负载 flaky（真实典型 <100ms）。
    EXPECT_LT(elapsed_ms, 5000) << "fast_exit must not wait for RUNNING task (took "
                                << elapsed_ms << "ms)";

    // RUNNING task 已 fail 善后（failed record 持久化 + 状态 FAILED）。
    auto failed = master.get_failed_tasks();
    EXPECT_NE(std::find(failed.begin(), failed.end(), 6000), failed.end())
        << "RUNNING task must be failed by fast_exit";

    // worker 收到 STOP_NOW（hook 拦截证据；生产语义为 kill 自身）。
    {
        std::unique_lock<std::mutex> lk(mtx);
        bool seen = cv.wait_for(lk, std::chrono::seconds(3),
                                [&] { return stop_now_seen; });
        EXPECT_TRUE(seen) << "worker should receive STOP_NOW";
        EXPECT_EQ(stop_now_reason, "test fast exit");
    }

    worker.stop_now_hook_for_testing_ = nullptr;
    worker.stop();
    Config::instance()->set_str("log_dir", "");
}

// record_worker_info 去重：同 (db_path, hostname, writer_id) tuple 只 append
// _DB_META 一次（ConcurrentUnorderedSet::insert 的"新插入才执行副作用"语义）。
// characterization：先锁行为再迁移到 ConcurrentUnorderedSet。
TEST(MasterAgentTest, RecordWorkerInfoAppendsMetaOncePerTuple) {
    fly::DataService::instance()->reset();
    ProcessInfo::instance()->set_hostname("test_host_dedup");
    TempDir tmpdir;

    // 不 start：record_worker_info（worker_id=0 路径）不走网络。
    MasterAgent master("127.0.0.1", 0);
    auto db_obj = master.get_or_create_database(tmpdir.path(), "", 0);
    ASSERT_NE(db_obj, nullptr);
    CMString db_path = db_obj->get_db_path();

    master.record_worker_info_for_testing("obj", db_path, 0, "w1");
    master.record_worker_info_for_testing("obj", db_path, 0, "w1");   // 同 tuple 重复
    EXPECT_EQ(master.recorded_workers_count_for_testing(), 1u);

    master.record_worker_info_for_testing("obj", db_path, 0, "w2");   // 不同 writer_id
    EXPECT_EQ(master.recorded_workers_count_for_testing(), 2u);
}

// ── expected workers（唤起占位符）────────────────────────────────────
// bsub 等慢调度场景：master 唤起 worker 后只登记占位符，不假设注册时限；
// RegisterMessage 到达即转正。全部用例确定性（直接驱动私有方法，无 sleep）。

TEST(MasterAgentTest, ExpectWorkerPlaceholderClearedOnRegister) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();  // on_worker_register 会 reactor_->send：需 reactor 已构造
    wait_for_running(master, true);

    master.expect_worker(1);
    EXPECT_EQ(master.get_expected_worker_count(), 1u);
    EXPECT_FALSE(master.all_workers_registered());

    RegisterMessage reg;
    reg.worker_id_ = 1;
    reg.data_server_port_ = 0;  // 跳过 DataService 登记
    master.inject_worker_register_for_testing(100, reg);  // conn 100 未知：send 安全 no-op

    EXPECT_TRUE(master.all_workers_registered());
    EXPECT_EQ(master.get_expected_worker_count(), 0u);

    // 清掉 fake 注册（conn 100 不存在、永不断连）：不清则 stop() 的 summary
    // 屏障 30s + 断连等待 10s 全部白等（实测 40.07s）。
    master.unregister_fake_worker_for_testing(1, 100);
    master.stop();
    wait_for_running(master, false);
}

TEST(MasterAgentTest, ExpectWorkerTimeoutCleanup) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("worker_register_timeout", 1);
    MasterAgent master("127.0.0.1", 0);

    master.expect_worker(99);  // 永不注册的占位符
    EXPECT_FALSE(master.all_workers_registered());

    // 1s 超时：遥远的检查点时间应清理（由 heartbeat_check_loop 周期调用，此处直接驱动）。
    master.check_expected_worker_timeouts_for_testing(9999999999LL);
    EXPECT_TRUE(master.all_workers_registered());
    EXPECT_EQ(master.get_expected_worker_count(), 0u);
    Config::instance()->set_int("worker_register_timeout", 0);
}

TEST(MasterAgentTest, ExpectWorkerDefaultNoTimeout) {
    fly::DataService::instance()->reset();
    Config::instance()->reset();  // 显式恢复默认（前序用例可能 set 过非默认值，Config 单例跨用例共享）
    // 默认 worker_register_timeout=0：master 占位符不等待不假设任何超时（用户确认
    // 语义——bsub 慢调度下 worker 任意时刻注册都被接受）。
    MasterAgent master("127.0.0.1", 0);

    master.expect_worker(99);
    // 任意遥远的检查点都不清理。
    master.check_expected_worker_timeouts_for_testing(9999999999LL);
    EXPECT_EQ(master.get_expected_worker_count(), 1u);
    EXPECT_FALSE(master.all_workers_registered());
}

TEST(MasterAgentTest, ExpectWorkerExplicitTimeoutCleans) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("worker_register_timeout", 300);  // 显式启用超时
    MasterAgent master("127.0.0.1", 0);

    master.expect_worker(99);  // spawn 时间戳 = 真实 now
    // 299s 后的检查点：仍在窗口内，不清理。
    master.check_expected_worker_timeouts_for_testing(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + 299);
    EXPECT_EQ(master.get_expected_worker_count(), 1u);

    // 301s 后的检查点：超时清理。
    master.check_expected_worker_timeouts_for_testing(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() + 301);
    EXPECT_EQ(master.get_expected_worker_count(), 0u);
    EXPECT_TRUE(master.all_workers_registered());
    Config::instance()->set_int("worker_register_timeout", 0);
}

// ── internal task 错误分支（execute_internal_task 的三个 TaskFailed 出口）──
// __backup_object 参数不足 / db_path 不可得、__merge_object 参数不足：经真实
// 派发 → worker 就地消化 → TaskFailed 收敛（不得崩溃、不得假成功）。
TEST(MasterAgentTest, InternalTaskFailureBranchesConvergeToTaskFailed) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // worker 的 task 执行依赖主循环 poll（真实环境由 Python 驱动），测试用
    // poll 线程模拟；internal task 由 take_task 就地消化。
    std::atomic<bool> poll_running{true};
    std::thread poll_thread([&] {
        while (poll_running.load() && worker.is_running()) {
            worker.poll_task_blocking(100);
        }
    });

    struct Case {
        uint64_t id;
        const char* name;
        CMVector<CMString> args;
        const char* expect_substr;
    };
    const Case cases[] = {
        {5101, "__backup_object", {"only_one_arg"}, "Internal backup: insufficient args"},
        {5102, "__backup_object", {"obj", "/nonexistent_db_zz"},
         "Internal backup: db_path request failed"},
        {5103, "__merge_object", {"a", "b", "c"}, "Internal merge: insufficient args"},
    };
    for (const auto& c : cases) {
        master.submit_task(c.id, c.name, "__fly_internal", c.args, {}, {});
    }
    for (const auto& c : cases) {
        bool reached = false;
        wait_for([&] {
            auto failed = master.get_failed_tasks();
            reached = std::find(failed.begin(), failed.end(), c.id) != failed.end();
            return reached;
        }, 300, 20);
        ASSERT_TRUE(reached) << "internal task " << c.id << " must converge to TaskFailed";
        EXPECT_NE(master.get_task_error(c.id).find(c.expect_substr), CMString::npos)
            << "task " << c.id << " error must carry the branch-specific reason";
    }
    EXPECT_EQ(master.get_completed_tasks().size(), 0u)
        << "broken internal tasks must not report success";

    poll_running.store(false);
    if (poll_thread.joinable()) poll_thread.join();
    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

// ── merge cleanup 屏障：worker 判死推进计数（settle 第 4 分支）────────
// 屏障 expected=1（唯一 fake worker）：cleanup_after_merge 阻塞等 ack →
// 判死把死亡 worker 视为已清理（received++ clamp）→ 等待收敛返回、条目擦除。
TEST(MasterAgentTest, MergeCleanupBarrierAdvancesOnWorkerDeath) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    master.register_fake_worker_for_testing(21, 5021);

    TempDir tmpdir;
    CMString db_path = tmpdir.path() + "/src_db";
    auto before = master.pending_merge_cleanup_counts_for_testing(db_path);
    EXPECT_EQ(before.first, 0u);
    EXPECT_EQ(before.second, 0u);

    std::thread cleaner([&] {
        master.cleanup_after_merge(db_path, {}, {}, {},
                                   tmpdir.path() + "/merged_db",
                                   tmpdir.path() + "/merged_data");
    });

    // 前置：屏障已登记为 {expected=1, received=0}（等 ack 的稳定态）。
    bool registered = false;
    for (int i = 0; i < 200 && !registered; ++i) {
        auto c = master.pending_merge_cleanup_counts_for_testing(db_path);
        registered = c.first == 1u && c.second == 0u;
        if (!registered) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(registered) << "cleanup barrier must be registered as {1,0}";

    // 判死 → settle 把死亡 worker 计为已清理 → 屏障达成 → cleanup 收敛。
    master.handle_worker_death_for_testing(21);
    cleaner.join();

    auto after = master.pending_merge_cleanup_counts_for_testing(db_path);
    EXPECT_EQ(after.first, 0u) << "satisfied barrier entry must be erased";
    master.unregister_fake_worker_for_testing(21, 5021);
    master.stop();
    wait_for_running(master, false);
}

// ── finish_task 的 write-rejection 联动（task 成功但写被拒）──────────
// 覆盖测试曾暴露的生产缺陷（已修复，本测试为回归锚）：finish_task 原实现
// 先调 end_task（其内部 WorkerAgentContext::clear() 把 last_error_type_ 重置
// 为 UNKNOWN）后才读取——SUCCESS 路径的 write-rejection 分支不可达，写被拒
// 的 task 被静默上报 TaskComplete（脏数据假成功）；异常失败路径的
// error_type_ 同理恒 UNKNOWN。修复：end_task 前快照 error_type。
TEST(MasterAgentTest, FinishTaskWriteRejectionFailsTaskAndAbortsWrites) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    TempDir tmpdir;
    CMString db_path = tmpdir.path() + "/rej_db";
    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    auto db = CMMakeShared<Database>(db_path, db_path + "/data", 0, "", db_path);
    worker.register_database(db_path, db);
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 在 master 元数据注册 task 42（assign 给唯一 worker 1）：TaskFailed
    // 上报需匹配 assign 记账，否则 on_task_failed 的 stale 防御会丢弃，
    // get_failed_tasks() 无法验证。assign 消息到达 worker 仅入 pending
    // 队列（无 poll_loop 消费），不影响本测试的本地 finish 路径。
    master.submit_task(42, "logical_success", "m", {"a"}, {}, {});
    bool assigned = false;
    wait_for([&] {
        const auto& running = master.get_running_tasks();
        assigned = std::find(running.begin(), running.end(), 42u) != running.end();
        return assigned;
    }, 100, 20);
    ASSERT_TRUE(assigned) << "task 42 must be assigned to the registered worker";

    // 模拟 executor 生命周期：begin → 记录脏写 → 注入 write-rejection →
    // SUCCESS 结果收尾。TLS 语义：begin/finish 同线程。
    worker.begin_task(42, "ctx_hash_rej");
    worker.record_write(db_path, "dirty_obj", 100);
    WorkerAgentContext::set_last_error_type(TaskErrorType::WRITE_TO_FROZEN_DB);

    PendingTask task;
    task.task_id_ = 42;
    task.task_name_ = "logical_success";
    task.task_module_ = "m";
    TaskExecResult result;
    result.task_id_ = 42;
    result.status_ = TaskExecStatus::SUCCESS;
    worker.finish_task(task, result);

    // 修复后语义：TaskFailed（带 error_type 与脏对象清单），绝非 TaskComplete。
    // TaskFailed 经网络异步到达 master——deadline 轮询等待落账。
    bool failed_reported = false;
    wait_for([&] {
        const auto& failed = master.get_failed_tasks();
        failed_reported = std::find(failed.begin(), failed.end(), 42u) != failed.end();
        return failed_reported;
    }, 100, 20);
    ASSERT_TRUE(failed_reported)
        << "rejected write must surface as TaskFailed (not silent TaskComplete)";
    EXPECT_EQ(master.get_completed_tasks().size(), 0u)
        << "rejected write must not produce a TaskComplete";
    // 注：finish_task 内部 end_task 会 clear TLS error_type——修复语义是
    // 「clear 前快照」，快照不可从外部观测；TaskFailed 落账即端到端证据。

    worker.stop();
    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();
    fly::DataService::instance()->unregister_database(db_path);
}

// ── master 进程内 var 三件套（setup_write_context 3660-3690 区域）────
// set/get/remove 全链 + 空 db_path（无 ':'）与未知 db 的拒绝。
TEST(MasterAgentTest, MasterVarServiceSetGetRemoveAndUnknownDb) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    master.setup_write_context();

    TempDir tmpdir;
    master.register_database(tmpdir.path(), tmpdir.path() + "/data");

    FlyBufferPtr v = CMMakeShared<FlyBuffer>();
    v->write("hello", 5);
    const CMString full = tmpdir.path() + ":v1";

    EXPECT_TRUE(WorkerAgentContext::set_var(full, v, "str"));
    {
        auto [ok, got, type_name] = WorkerAgentContext::get_var(full);
        EXPECT_TRUE(ok);
        ASSERT_NE(got, nullptr);
        EXPECT_EQ(CMString(got->data(), got->size()), "hello");
        EXPECT_EQ(type_name, "str");
    }
    WorkerAgentContext::remove_var(full);
    {
        auto [ok, got, type_name] = WorkerAgentContext::get_var(full);
        EXPECT_FALSE(ok) << "removed var must be gone";
        EXPECT_EQ(got, nullptr);
    }

    // 错误路径：无 ':' 的裸名 / 未知 db → false（get 的值/类型为空）。
    EXPECT_FALSE(WorkerAgentContext::set_var("no_colon_name", v, "str"));
    {
        auto [ok, got, type_name] = WorkerAgentContext::get_var("no_colon_name");
        EXPECT_FALSE(ok);
        EXPECT_EQ(got, nullptr);
    }
    EXPECT_FALSE(WorkerAgentContext::set_var(db32("no_such_db") + ":v", v, "str"));
    EXPECT_FALSE(std::get<0>(WorkerAgentContext::get_var(db32("no_such_db") + ":v")));

    WorkerAgentContext::clear();
    master.stop();
    wait_for_running(master, false);
}

// ── var 服务错误路径（经真实网络 ack 链）────────────────────────────
// 未注册 db 的 set/get → VarAck success=false；frozen db 的 set → "db frozen"；
// 不可变 var 二次 set → 拒绝 + reject 广播。
TEST(MasterAgentTest, VarServiceErrorPathsOverNetwork) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    TempDir tmpdir;
    CMString db_path = tmpdir.path() + "/var_db";
    master.register_database(db_path, db_path + "/data");

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    FlyBufferPtr v = CMMakeShared<FlyBuffer>();
    v->write("val", 3);

    // 未注册 db：set/get 均被 master 拒（db not found on master）。
    EXPECT_FALSE(worker.set_var_sync(db32("var_missing") + ":v", v, "str"));
    EXPECT_FALSE(std::get<0>(worker.get_var_sync(db32("var_missing") + ":v")));

    // 正常 set → 不可变：二次 set 拒绝。
    EXPECT_TRUE(worker.set_var_sync(db_path + ":v", v, "str"));
    EXPECT_FALSE(worker.set_var_sync(db_path + ":v", v, "str"))
        << "immutable var must reject the second set";

    // get 未命中。
    EXPECT_FALSE(std::get<0>(worker.get_var_sync(db_path + ":missing")));

    // frozen db 的 set：master_set_var 拒绝 + ack "db frozen"。
    master.get_database(db_path)->freeze();
    wait_for([&] { return master.is_db_frozen(db_path); }, 50, 20);
    EXPECT_FALSE(worker.set_var_sync(db_path + ":v2", v, "str"))
        << "set on a frozen db must be rejected";

    worker.stop();
    master.stop();
    wait_for_running(master, false);
    fly::DataService::instance()->unregister_database(db_path);
}

// ── 重复注册防护：探测发送失败 = 旧 conn 已死 → 当场接受为重连注册 ──
// （on_worker_register 的 probe-fail 分支：不挂起、不等 15s deadline——handler
//  lane 并行下 deferred 条目可能孤儿化的根治路径。）
TEST(MasterAgentTest, DuplicateRegisterProbeFailAcceptedAsReconnect) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // worker_id=5 已有「活跃连接」——fake conn 7001（transport 未知 → send 必败）。
    master.register_fake_worker_for_testing(5, 7001);

    RegisterMessage reg;
    reg.worker_id_ = 5;
    reg.data_server_port_ = 0;   // 跳过 DataService 登记
    master.inject_worker_register_for_testing(/*conn_id=*/2002, reg);

    // 新注册被当场接受：conn 映射切到 2002、worker 进池为 IDLE。
    auto connected = master.get_connected_workers();
    ASSERT_EQ(connected.size(), 1u);
    EXPECT_EQ(connected[0], 5u);
    EXPECT_EQ(master.worker_status_for_testing(5), WorkerStatus::IDLE)
        << "probe-fail must fall through to the normal register path";

    master.unregister_fake_worker_for_testing(5, 7001);
    master.stop();
    wait_for_running(master, false);
}

// 非法 role 上报 → hybrid 回退（进调度候选，不进 storage_only 名单）。
TEST(MasterAgentTest, UnknownRoleFallsBackToHybrid) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    RegisterMessage reg;
    reg.worker_id_ = 6;
    reg.data_server_port_ = 0;
    reg.role_ = 200;   // 非法值（合法：0=hybrid, 1=storage_only）
    master.inject_worker_register_for_testing(/*conn_id=*/2003, reg);

    bool idle = false;
    wait_for([&] {
        auto idle_workers = master.get_idle_workers();
        idle = std::find(idle_workers.begin(), idle_workers.end(), 6u) != idle_workers.end();
        return idle;
    }, 100, 10);
    EXPECT_TRUE(idle) << "fallback-to-hybrid worker must be schedulable";
    auto storage_only = master.get_storage_only_workers();
    EXPECT_EQ(std::find(storage_only.begin(), storage_only.end(), 6u),
              storage_only.end())
        << "illegal role must not be classified as storage_only";

    master.unregister_fake_worker_for_testing(6, 2003);
    master.stop();
    wait_for_running(master, false);
}

// ── 迟到上报防串扰 + 不可恢复失败 message 分支────────────────────────
// task 从 W1 重排队给 W2 后，W1 的迟到 complete/failed 必须整体丢弃；
// WRITE_REGISTRATION_TIMEOUT 类失败（W2 上报）走 FATAL 分支正常收敛。
TEST(MasterAgentTest, StaleReportDroppedAndUnrecoverableFailedEmits) {
    fly::DataService::instance()->reset();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    master.register_fake_worker_for_testing(1, 5001);
    master.submit_task(5201, "migrating_task", "m", {"a"}, {}, {});
    bool running = false;
    for (int i = 0; i < 100 && !running; ++i) {
        auto tasks = master.get_running_tasks();
        running = std::find(tasks.begin(), tasks.end(), 5201u) != tasks.end();
        if (!running) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(running) << "task must be assigned to the only worker";

    // W1 判死 → task 重排队 → 立即调度给 W2（判死处理器内同步完成）。
    master.register_fake_worker_for_testing(2, 5002);
    master.handle_worker_death_for_testing(1);
    running = false;
    for (int i = 0; i < 100 && !running; ++i) {
        auto tasks = master.get_running_tasks();
        running = std::find(tasks.begin(), tasks.end(), 5201u) != tasks.end();
        if (!running) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(running) << "requeued task must be reassigned after death";

    // W1 的迟到上报（complete/failed 同理）：assigned 已是 W2 → 整体丢弃。
    {
        TaskCompleteMessage stale;
        stale.task_id_ = 5201;
        stale.worker_id_ = 1;
        master.on_task_complete(0, stale);
    }
    {
        TaskFailedMessage stale;
        stale.task_id_ = 5201;
        stale.worker_id_ = 1;
        stale.error_message_ = "stale failure";
        master.on_task_failed(0, stale);
    }
    EXPECT_EQ(master.get_completed_tasks().size(), 0u)
        << "stale complete must be dropped";
    EXPECT_EQ(master.get_failed_tasks().size(), 0u)
        << "stale failure must be dropped";
    running = false;
    for (int i = 0; i < 50 && !running; ++i) {
        auto tasks = master.get_running_tasks();
        running = std::find(tasks.begin(), tasks.end(), 5201u) != tasks.end();
        if (!running) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(running) << "task must stay RUNNING on W2 after stale reports";

    // W2 的真失败（不可恢复类型）：正常收敛 + FATAL message 分支。
    TaskFailedMessage fatal;
    fatal.task_id_ = 5201;
    fatal.worker_id_ = 2;
    fatal.error_message_ = "unrecoverable boom";
    fatal.error_type_ = TaskErrorType::WRITE_REGISTRATION_TIMEOUT;
    master.on_task_failed(0, fatal);
    bool failed = false;
    wait_for([&] {
        auto tasks = master.get_failed_tasks();
        failed = std::find(tasks.begin(), tasks.end(), 5201u) != tasks.end();
        return failed;
    }, 100, 10);
    EXPECT_TRUE(failed);
    EXPECT_EQ(master.get_task_error(5201), "unrecoverable boom");

    master.unregister_fake_worker_for_testing(1, 5001);
    master.unregister_fake_worker_for_testing(2, 5002);
    master.stop();
    wait_for_running(master, false);
}

// ── restart_failed_tasks 边界（不存在 / 空 bin / 旧 3 段格式 / 摘除清空删文件）──
TEST(MasterAgentTest, RestartFailedTasksBoundaryCases) {
    const CMString prev_log_dir = Config::instance()->get_str("log_dir");
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    // ① bin 不存在 → 0。
    EXPECT_EQ(master.restart_failed_tasks(tmpdir.path() + "/nope.bin"), 0u);

    // ② 空 bin → 0（文件保留）。
    {
        std::ofstream ofs(tmpdir.path() + "/empty.bin", std::ios::binary);
    }
    EXPECT_EQ(master.restart_failed_tasks(tmpdir.path() + "/empty.bin"), 0u);
    EXPECT_TRUE(std::filesystem::exists(tmpdir.path() + "/empty.bin"));

    // ③ 旧 3 段格式（无 uid）→ 文件级拒绝 0（文件保留）。
    {
        FailedTaskRecord legacy;
        legacy.task_id_ = 5301;
        legacy.submission_.name_ = "legacy_task";
        legacy.submission_.args_ = {"__fly_db__:/tmp/legacy_db:/tmp/legacy_data"};
        CMString body;
        FLY_ENCODE(legacy, body);
        int64_t body_size = static_cast<int64_t>(body.size());
        std::ofstream ofs(tmpdir.path() + "/legacy.bin", std::ios::binary | std::ios::app);
        ofs.write(reinterpret_cast<const char*>(&body_size), sizeof(body_size));
        ofs.write(body.data(), body.size());
    }
    EXPECT_EQ(master.restart_failed_tasks(tmpdir.path() + "/legacy.bin"), 0u)
        << "legacy no-uid records must reject the whole bin";
    EXPECT_TRUE(std::filesystem::exists(tmpdir.path() + "/legacy.bin"))
        << "rejected bin must be preserved";

    // ④ 唯一记录的 task 成功 → failed_tasks.bin 摘空删除。
    master.register_fake_worker_for_testing(41, 5041);
    master.submit_task(5302, "persisted_task", "m", {"a"}, {}, {});
    FailedTaskRecord rec;
    rec.task_id_ = 5302;
    rec.submission_.name_ = "persisted_task";
    master.persist_failed_task_for_testing(rec);
    const CMString bin = tmpdir.path() + "/failed_tasks.bin";
    ASSERT_TRUE(std::filesystem::exists(bin));
    TaskCompleteMessage complete;
    complete.task_id_ = 5302;
    complete.worker_id_ = 41;
    master.on_task_complete(0, complete);
    EXPECT_FALSE(std::filesystem::exists(bin))
        << "removing the last record must delete the empty bin";

    master.unregister_fake_worker_for_testing(41, 5041);
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_str("log_dir", prev_log_dir);
}

// ── 心跳判死 → Shutdown 下发 → 心跳复活（REVIVED）──────────────────
// heartbeat_timeout=1s + 永不心跳的 fake worker：检查线程（5s 周期）标死；
// 随后任意心跳到达把终态 worker 复活为 IDLE。
TEST(MasterAgentTest, HeartbeatTimeoutFlagsDeadThenRevives) {
    Config::instance()->set_int("heartbeat_timeout", 1);
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    master.register_fake_worker_for_testing(9, 5009);

    bool dead = false;
    for (int i = 0; i < 500 && !dead; ++i) {   // 检查线程 5s 一轮，20s 上界
        dead = master.worker_status_for_testing(9) == WorkerStatus::DEAD;
        if (!dead) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    EXPECT_TRUE(dead) << "silent worker must be flagged dead after heartbeat_timeout";

    HeartbeatMessage hb;
    hb.worker_id_ = 9;
    master.on_heartbeat(0, hb);
    EXPECT_EQ(master.worker_status_for_testing(9), WorkerStatus::IDLE)
        << "heartbeat after death must revive the worker (REVIVED)";

    master.unregister_fake_worker_for_testing(9, 5009);
    master.stop();
    wait_for_running(master, false);
    Config::instance()->set_int("heartbeat_timeout", 120);
}

// ── drain 打断：stop() 优雅等待被 fast_exit 打断转快速路径 ───────────
// 不可完成的 RUNNING task 挂住 drain；并发 fast_exit 置打断标志 → drain
// 立即转 fast（fail 善后 + 收尾），不再等任务完成。
TEST(MasterAgentTest, FastExitInterruptsDrainWait) {
    fly::DataService::instance()->reset();
    const CMString prev_log_dir = Config::instance()->get_str("log_dir");
    TempDir tmpdir;
    Config::instance()->set_str("log_dir", tmpdir.path());

    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);
    master.register_fake_worker_for_testing(31, 5031);

    master.submit_task(700, "hang_task", "m", {"a"}, {}, {});
    bool running = false;
    for (int i = 0; i < 100 && !running; ++i) {
        auto tasks = master.get_running_tasks();
        running = std::find(tasks.begin(), tasks.end(), 700u) != tasks.end();
        if (!running) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(running) << "task must be RUNNING to hold the drain";

    std::thread stopper([&] { master.stop(); });
    // 300ms 后 drain 已在 cv 等待（600s 上限内）；fast_exit 置打断标志。
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    master.fast_exit("interrupt-drain");
    stopper.join();
    wait_for_running(master, false);

    auto failed = master.get_failed_tasks();
    EXPECT_NE(std::find(failed.begin(), failed.end(), 700u), failed.end())
        << "interrupted drain must fail the hanging task (fast path aftermath)";

    master.unregister_fake_worker_for_testing(31, 5031);
    Config::instance()->set_str("log_dir", prev_log_dir);
}

}  // namespace fly
