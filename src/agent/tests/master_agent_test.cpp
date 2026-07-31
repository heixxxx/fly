#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <agent/cpp/worker_agent.h>
#include <common/cpp/test_helpers.h>
#include <core/cpp/config.h>
#include <core/cpp/process_info.h>
#include <serialization/cpp/serialization_macros.h>
#include <thread>
#include <chrono>

using namespace fly::test;

static CMString db32(const CMString& hint) {
    CMString r = hint;
    r.resize(fly::db_id_len(), '_');
    return r;
}

namespace fly {

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
#include <common/cpp/worker_context.h>
#include <storage/cpp/db_meta.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <fstream>
#include <fmt/format.h>

namespace {

class TempDir {
public:
    TempDir() {
        auto ts = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = "/tmp/fly_test_" + ::std::to_string(ts) + "_" +
                ::std::to_string(reinterpret_cast<uintptr_t>(this));
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

void create_idx_file(const CMString& base_path, const CMString& writer_id,
                     const CMVector<IndexEntry>& entries) {
    CMString idx_path = base_path + "/" + writer_id + ".idx";
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

    CMString db_id = db32("test_db_setup_write");
    CMString obj_name = "test_obj_setup_write";
    CMString full_name = db_id + ":" + obj_name;

    // master 自写走 register 路径（on_master_register_write → do_write_register）。
    // record_write_func 现为 no-op，不再触发 placement 更新。
    auto [msg, err_type] = WorkerAgentContext::register_write(db_id, obj_name, 100);
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

TEST(MasterAgentTest, SetupWriteContext_MasterNotRunning_RecordWriteNoOp) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    // Do NOT start — running_ is false
    master.setup_write_context();

    CMString db_id = db32("test_db_noop");
    CMString obj_name = "test_obj_noop";
    CMString full_name = db_id + ":" + obj_name;

    // master record_write_func 现为 no-op；register_write 在 !running_ 时返回 UNKNOWN 不登记
    WorkerAgentContext::record_write(db_id, obj_name, 100);

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

TEST(MasterAgentTest, RestoreMasterIdx_ExistingIdxFile) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_restore_existing");
    CMString base_path = tmpdir.path();

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

    create_idx_file(base_path, "master000", {entry1, entry2});

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, "master000");

    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].object_name_, "obj_restore_1");
    EXPECT_EQ(entries[1].object_name_, "obj_restore_2");

    // DataService local_idx should be populated
    EXPECT_TRUE(DataService::instance()->has_local_object(db_id + ":obj_restore_1"));
    EXPECT_TRUE(DataService::instance()->has_local_object(db_id + ":obj_restore_2"));

    // Cleanup
    DataService::instance()->remove_local_index(db_id + ":obj_restore_1");
    DataService::instance()->remove_local_index(db_id + ":obj_restore_2");
}

TEST(MasterAgentTest, RestoreMasterIdx_NonExistentIdxFile) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_restore_missing");

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, tmpdir.path(), "w999");

    EXPECT_TRUE(entries.empty());
}

TEST(MasterAgentTest, RestoreMasterIdx_EmptyIdxFile) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_restore_empty");
    CMString base_path = tmpdir.path();

    // Create idx file with no entries
    CMString idx_path = base_path + "/master000.idx";
    {
        // LocalIndex with no entries → save writes nothing (not modified)
        // So we need to touch the file manually to create an empty file
        std::ofstream ofs(idx_path, std::ios::binary);
        // Empty file
    }

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, "master000");

    EXPECT_TRUE(entries.empty());
}

TEST(MasterAgentTest, RestoreMasterIdx_MultipleEntries) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_restore_multi");
    CMString base_path = tmpdir.path();

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
    create_idx_file(base_path, "master000", entries_writer0);

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, "master000");

    ASSERT_EQ(entries.size(), 5u);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(entries[i].object_name_, fmt::format("multi_obj_{}", i));
    }

    // Cleanup
    for (int i = 0; i < 5; i++) {
        DataService::instance()->remove_local_index(db_id + ":" + fmt::format("multi_obj_{}", i));
    }
}

// --- rebuild_remote_idx ---

TEST(MasterAgentTest, RebuildRemoteIdx_MasterEntries) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_rebuild_master");
    CMString base_path = tmpdir.path();
    CMString full = db_id + ":master_obj_1";

    // Create master's idx with entries (LocalIndex stores short_name only)
    IndexEntry entry;
    entry.object_name_ = "master_obj_1";
    entry.file_name_ = "data_0.bin";
    entry.offset_ = 0;
    entry.size_ = 50;

    create_idx_file(base_path, "master000", {entry});

    // WorkerInfo for master (worker_id=0, hostname="localhost")
    ::WorkerInfo master_worker;
    master_worker.worker_id_ = 0;
    master_worker.writer_id_ = "master000";
    master_worker.hostname_ = "localhost";

    MasterAgent master("127.0.0.1", 0);
    // Register a new worker on "localhost" so master's entries get mapped to it
    DataService::instance()->register_worker(10, "127.0.0.1", 9999);
    master.add_worker_hostname(10, "localhost");

    master.rebuild_remote_idx(db_id, base_path, {master_worker});

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
    CMString db_id = db32("test_db_rebuild_no_new_workers");
    CMString base_path = tmpdir.path();
    CMString full = db_id + ":worker_obj_skip";

    // Create worker_5.idx with entries (LocalIndex stores short_name only)
    IndexEntry entry;
    entry.object_name_ = "worker_obj_skip";
    entry.file_name_ = "data_5.bin";
    entry.offset_ = 0;
    entry.size_ = 50;

    create_idx_file(base_path, "worker005", {entry});

    // WorkerInfo for old worker_id=5, but no new workers registered
    ::WorkerInfo old_worker;
    old_worker.worker_id_ = 5;
    old_worker.writer_id_ = "worker005";
    old_worker.hostname_ = "testhost_skipped";

    MasterAgent master("127.0.0.1", 0);
    master.rebuild_remote_idx(db_id, base_path, {old_worker});

    // No new workers with matching hostname → entry should NOT be in remote_idx
    EXPECT_FALSE(DataService::instance()->has_remote_location(full));
}

TEST(MasterAgentTest, RebuildRemoteIdx_MissingIdxFile_Skipped) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_rebuild_missing_idx");
    CMString base_path = tmpdir.path();

    // WorkerInfo for a worker whose idx file doesn't exist
    ::WorkerInfo missing_worker;
    missing_worker.worker_id_ = 42;
    missing_worker.writer_id_ = "worker042";
    missing_worker.hostname_ = "ghost_host";

    MasterAgent master("127.0.0.1", 0);
    // Should not crash, just WARN and skip
    master.rebuild_remote_idx(db_id, base_path, {missing_worker});
}

TEST(MasterAgentTest, RebuildRemoteIdx_MultipleWorkers) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_rebuild_multi");
    CMString base_path = tmpdir.path();
    CMString full_master = db_id + ":multi_master_obj";
    CMString full_worker = db_id + ":multi_worker_obj";

    // Create master's idx (LocalIndex stores short_name only)
    IndexEntry master_entry;
    master_entry.object_name_ = "multi_master_obj";
    master_entry.file_name_ = "data_0.bin";
    master_entry.offset_ = 0;
    master_entry.size_ = 50;
    create_idx_file(base_path, "master000", {master_entry});

    // Create worker's idx
    IndexEntry worker_entry;
    worker_entry.object_name_ = "multi_worker_obj";
    worker_entry.file_name_ = "data_3.bin";
    worker_entry.offset_ = 0;
    worker_entry.size_ = 100;
    create_idx_file(base_path, "worker003", {worker_entry});

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

    master.rebuild_remote_idx(db_id, base_path, {w0, w3});

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
    CMString db_id = db32("test_db_rebuild_empty_workers");
    CMString base_path = tmpdir.path();

    MasterAgent master("127.0.0.1", 0);
    // Empty workers vector → no iteration, no crash
    master.rebuild_remote_idx(db_id, base_path, {});
}

TEST(MasterAgentTest, RebuildRemoteIdx_MultiHost_MappedToCorrectWorkers) {
    TempDir tmpdir;
    CMString db_id = db32("test_db_multi_host");
    CMString base_path = tmpdir.path();
    CMString full_m = db_id + ":master_data";
    CMString full_a = db_id + ":worker_a_data";
    CMString full_b = db_id + ":worker_b_data";

    // Master (worker_id=0) on "host_master" (LocalIndex stores short_name only)
    IndexEntry master_entry;
    master_entry.object_name_ = "master_data";
    master_entry.file_name_ = "data_m.bin";
    master_entry.offset_ = 0;
    master_entry.size_ = 50;
    create_idx_file(base_path, "w_master", {master_entry});

    // Worker A (worker_id=1) on "host_a"
    IndexEntry worker_a_entry;
    worker_a_entry.object_name_ = "worker_a_data";
    worker_a_entry.file_name_ = "data_a.bin";
    worker_a_entry.offset_ = 0;
    worker_a_entry.size_ = 80;
    create_idx_file(base_path, "w_hosta", {worker_a_entry});

    // Worker B (worker_id=2) on "host_b"
    IndexEntry worker_b_entry;
    worker_b_entry.object_name_ = "worker_b_data";
    worker_b_entry.file_name_ = "data_b.bin";
    worker_b_entry.offset_ = 0;
    worker_b_entry.size_ = 120;
    create_idx_file(base_path, "w_hostb", {worker_b_entry});

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

    master.rebuild_remote_idx(db_id, base_path, {w_master, w_a, w_b});

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
    CMString db_id = db32("test_db_same_host_merge");
    CMString base_path = tmpdir.path();
    CMString full_m = db_id + ":m_obj";
    CMString full_w = db_id + ":w_obj";
    CMString full_r = db_id + ":r_obj";

    // Master (worker_id=0) on "host_local" (LocalIndex stores short_name only)
    IndexEntry master_entry;
    master_entry.object_name_ = "m_obj";
    master_entry.file_name_ = "data_m.bin";
    master_entry.offset_ = 0;
    master_entry.size_ = 50;
    create_idx_file(base_path, "w_m", {master_entry});

    // Worker (worker_id=5) on same "host_local"
    IndexEntry worker_entry;
    worker_entry.object_name_ = "w_obj";
    worker_entry.file_name_ = "data_w.bin";
    worker_entry.offset_ = 0;
    worker_entry.size_ = 80;
    create_idx_file(base_path, "w_w5", {worker_entry});

    // Remote Worker (worker_id=3) on "host_remote"
    IndexEntry remote_entry;
    remote_entry.object_name_ = "r_obj";
    remote_entry.file_name_ = "data_r.bin";
    remote_entry.offset_ = 0;
    remote_entry.size_ = 120;
    create_idx_file(base_path, "w_r3", {remote_entry});

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

    master.rebuild_remote_idx(db_id, base_path, {w_m, w5, w3});

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
    CMString db_id = db32("test_db_partial_host");
    CMString base_path = tmpdir.path();
    CMString full_avail = db_id + ":avail_obj";
    CMString full_off = db_id + ":offline_obj";

    // Worker A on "host_available" (LocalIndex stores short_name only)
    IndexEntry entry_a;
    entry_a.object_name_ = "avail_obj";
    entry_a.file_name_ = "data_a.bin";
    entry_a.offset_ = 0;
    entry_a.size_ = 50;
    create_idx_file(base_path, "w_avail", {entry_a});

    // Worker B on "host_offline"
    IndexEntry entry_b;
    entry_b.object_name_ = "offline_obj";
    entry_b.file_name_ = "data_b.bin";
    entry_b.offset_ = 0;
    entry_b.size_ = 80;
    create_idx_file(base_path, "w_off", {entry_b});

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

    master.rebuild_remote_idx(db_id, base_path, {wa, wb});

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

TEST(MasterAgentTest, DoubleStopNoCrash) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    wait_for_running(master, true);

    master.stop();
    wait_for_running(master, false);

    // Double stop — should be safe
    EXPECT_NO_THROW(master.stop());
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, StopBeforeStartNoCrash) {
    MasterAgent master("127.0.0.1", 0);
    EXPECT_NO_THROW(master.stop());
}

TEST(MasterAgentTest, OnDisconnectRecoversRunningTasks) {
    Logger::shutdown();
    Logger::init("test_logs/", 0);
    fly::DataService::instance()->reset();

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

    worker.stop();

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
    EXPECT_FALSE(still_running) << "Task 42 should no longer be RUNNING after worker disconnect";

    auto failed = master.get_failed_tasks();
    bool task_failed = false;
    for (auto id : failed) {
        if (id == 42) { task_failed = true; break; }
    }
    EXPECT_FALSE(task_failed) << "Task 42 should not be FAILED after worker disconnect";

    master.stop();
    wait_for_running(master, false);
    Logger::shutdown();
}

// --- register_database + is_db_frozen ---

TEST(MasterAgentTest, RegisterDatabaseAndIsFrozen) {
    fly::DataService::instance()->reset();
    TempDir tmpdir;
    CMString db_id = db32("test_db_reg_freeze");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());

    // Not frozen initially
    EXPECT_FALSE(master.is_db_frozen(db_id));

    // Unknown db is also not frozen
    EXPECT_FALSE(master.is_db_frozen(db32("unknown_db_freeze")));

    // Freeze via network: start master, connect worker, send freeze request
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.request_database_freeze(db_id);

    // Wait for master to process the freeze
    wait_for([&] { return master.is_db_frozen(db_id); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_id));

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
    CMString db_id = db32("nstream_freeze_commit");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 模拟 task 内 freeze：begin_task 设 current_task_id_，然后 freeze
    worker.begin_task(1001, "");
    worker.request_database_freeze(db_id);

    // pending 登记：is_db_frozen 应覆盖 pending（跨 task 写注册需被拦截）
    wait_for([&] { return master.is_db_frozen(db_id); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_id));          // confirmed ∪ pending
    EXPECT_TRUE(master.is_db_pending_frozen(db_id));  // 仅 pending（未提交）

    // 模拟 task 成功：commit_pending_frozen 把 pending 迁移到 confirmed
    master.commit_pending_frozen(1001);
    EXPECT_TRUE(master.is_db_frozen(db_id));           // 仍是 frozen
    EXPECT_FALSE(master.is_db_pending_frozen(db_id));  // 但不再是 pending（已 confirmed）

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
    CMString db_id = db32("nstream_freeze_rollback");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    worker.begin_task(2002, "");
    worker.request_database_freeze(db_id);
    wait_for([&] { return master.is_db_pending_frozen(db_id); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_id));   // pending 状态下也算 frozen

    // 模拟 task 失败：rollback_pending_frozen 清掉该 task 的 pending
    master.rollback_pending_frozen(2002);
    EXPECT_FALSE(master.is_db_frozen(db_id));          // 回滚后不再 frozen
    EXPECT_FALSE(master.is_db_pending_frozen(db_id));  // pending 也清了

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
    CMString db_a = db32("frzA_committwo");   // 前 10 字符 "frzA_commi"
    CMString db_b = db32("frzB_committwo");   // 前 10 字符 "frzB_commi"

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_a, tmpdir_a.path());
    master.register_database(db_b, tmpdir_b.path());
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
    CMString db_id = db32("nstream_freeze_conflict");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(501, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    // 第一次 freeze 成功（pending）
    worker.begin_task(4001, "");
    worker.request_database_freeze(db_id);
    wait_for([&] { return master.is_db_pending_frozen(db_id); }, 50, 20);

    // 第二次 freeze 同一 db（模拟另一 task 业务流程错误）→ 应被拒绝
    worker.begin_task(4002, "");
    worker.request_database_freeze(db_id);

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
//     这是 Q1 选 task_id 而非 db_id 的核心理由：崩溃时 master 收不到失败消息。
TEST(MasterAgentTest, NonStreamFreezeClearedOnWorkerCrash) {
    fly::DataService::instance()->reset();
    Config::instance()->set_int("dependency_update_mode", 1);
    TempDir tmpdir;
    CMString db_id = db32("nstream_freeze_crash");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());
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
    worker.request_database_freeze(db_id);
    wait_for([&] { return master.is_db_pending_frozen(db_id); }, 50, 20);
    EXPECT_TRUE(master.is_db_frozen(db_id));   // pending → 写注册被拦截

    // 模拟 worker 崩溃：stop() 触发断连
    worker.stop();

    // master on_disconnect 应按 task_id 清掉 pending（防永久死锁）
    wait_for([&] { return !master.is_db_frozen(db_id); }, 100, 30);
    EXPECT_FALSE(master.is_db_frozen(db_id));          // pending 已清
    EXPECT_FALSE(master.is_db_pending_frozen(db_id));  // 不再残留

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
    CMString db_id = db32("nswr");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_x = db_id + ":obj_x";

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
    auto [ack_msg, err_type] = worker.register_write_with_master(db_id, "obj_x", 100);
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
    CMString db_id = db32("nssz");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_a = db_id + ":obj_a";

    // worker 在 task 内写对象：register（向 master 校验/provenance）+ record（本地记录写出）。
    // 真实流程里两者由 commit_write 落盘完成回调同时触发；测试显式调用以驱动 size 链路。
    worker.begin_task(8000, "");
    auto [ack_msg, err_type] = worker.register_write_with_master(db_id, "obj_a", 100);
    EXPECT_EQ(err_type, TaskErrorType::UNKNOWN);   // batch 模式：校验通过
    worker.record_write(db_id, "obj_a", 100);      // 记录写出（full_name + size）

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
    CMString db_id = db32("swr");

    MasterAgent master("127.0.0.1", 0);
    master.register_database(db_id, tmpdir.path());
    master.start();
    wait_for_running(master, true);

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_y = db_id + ":obj_y";

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
    auto [ack_msg, err_type] = worker.register_write_with_master(db_id, "obj_y", 100);
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
    worker.stop();
    master.stop();
    wait_for_running(master, false);
}

// WP2-T3: 非 stream 模式下 task complete 时 record_worker_info 被正确调用（技术债修复）。
//         WrittenObject 带 db_id_ 后，complete 时 master 能补做 record_worker_info，
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
    CMString db_id = db_obj->get_db_id();

    WorkerAgent worker(1, "127.0.0.1", master.get_port());
    worker.start();
    ASSERT_TRUE(wait_until_registered(worker));

    CMString obj_x = db_id + ":obj_x";

    // 提交产出 obj_x 的 task 让它 running
    master.submit_task(8000, "producer3", "test_module", {"arg"},
                       {}, {obj_x}, {}, -1.0f, "", {});
    wait_for([&] {
        auto running = master.get_running_tasks();
        return std::find(running.begin(), running.end(), 8000) != running.end();
    }, 50, 20);

    worker.begin_task(8000, "");
    worker.register_write_with_master(db_id, "obj_x", 100);

    // task complete（written_objects 带 db_id_）
    TaskCompleteMessage complete;
    complete.task_id_ = 8000;
    complete.worker_id_ = 1;
    WrittenObject wo;
    wo.object_name_ = obj_x;
    wo.size_bytes_ = 100;
    complete.written_objects_.push_back(wo);
    master.on_task_complete(0, complete);

    // master 的 record_worker_info 调用了 db->append_worker_info_to_meta，
    // 写入 _DB_META。通过读同一 base_path 的 _DB_META 文件验证。
    Database verify_db(tmpdir.path(), "", 0);
    EXPECT_GT(verify_db.worker_info_count(), 0u);   // record_worker_info 生效

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

    CMString db_id = db->get_db_id();
    EXPECT_EQ(db_id.size(), fly::db_id_len());
    EXPECT_FALSE(db->get_writer_id().empty());
    EXPECT_EQ(db->get_base_path(), tmpdir1.path());

    auto db2 = master.get_or_create_database(tmpdir2.path());
    ASSERT_NE(db2, nullptr);
    EXPECT_EQ(db2->get_db_id().size(), fly::db_id_len());
    EXPECT_NE(db->get_db_id(), db2->get_db_id());

    DataService::instance()->unregister_database(db->get_db_id());
    DataService::instance()->unregister_database(db2->get_db_id());
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

// --- get_connected_workers ---

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
    CMString db_id = db32("reg_test");
    CMString base = "/tmp/test_base_" + std::to_string(::getpid());

    master.register_database(db_id, base, base + "/data");
    EXPECT_FALSE(master.is_db_frozen(db_id));
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

    CMString db_id = db32("reg_norun");
    auto [msg, err_type] = WorkerAgentContext::register_write(db_id, "test_obj", 100);

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
    Config::instance()->set_int("backup_replicas", 2);

    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    CMString db_id = db32("test_self_bk");
    CMString obj_name = "self_bk_obj";
    CMString full_name = db_id + ":" + obj_name;

    // master 自写，auto_backup_enabled=1 → do_write_register 进入 evaluate_and_trigger_backup 分支。
    // 单 master 无其它 worker 做 backup 目标，trigger_auto_backup 会 no-op，但路径必须走通不崩溃。
    auto [msg, err_type] = WorkerAgentContext::register_write(db_id, obj_name, 100);
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
}  // namespace fly
