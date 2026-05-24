#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <common/cpp/test_helpers.h>
#include <thread>
#include <chrono>

using namespace fly::test;

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
    EXPECT_NE(WorkerAgentContext::current_record_ctx(), nullptr);

    WorkerAgentContext::clear();
}

TEST(MasterAgentTest, SetupWriteContext_MasterRunning_RecordWriteUpdatesRemoteIdx) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    master.start();
    master.setup_write_context();
    wait_for_running(master, true);

    CMString db_id = "test_db_setup_write";
    CMString obj_name = "test_obj_setup_write";
    CMString full_name = db_id + ":" + obj_name;

    // Record write via WorkerAgentContext → triggers on_master_record_write
    WorkerAgentContext::record_write(db_id, obj_name);

    // on_data_ready should have updated remote_idx
    EXPECT_TRUE(DataService::instance().has_remote_location(full_name));
    auto info = DataService::instance().lookup_remote_idx(full_name);
    EXPECT_EQ(info.worker_id, 0u);

    master.stop();
    wait_for_running(master, false);
    WorkerAgentContext::clear();

    // Cleanup singleton state
    DataService::instance().remove_remote_index(full_name);
}

TEST(MasterAgentTest, SetupWriteContext_MasterNotRunning_RecordWriteNoOp) {
    WorkerAgentContext::clear();
    MasterAgent master("127.0.0.1", 0);
    // Do NOT start — running_ is false
    master.setup_write_context();

    CMString db_id = "test_db_noop";
    CMString obj_name = "test_obj_noop";
    CMString full_name = db_id + ":" + obj_name;

    // on_master_record_write returns early when !running_
    WorkerAgentContext::record_write(db_id, obj_name);

    // remote_idx should NOT be updated (worker_id=0 not registered, so addr is empty)
    // on_data_ready still runs — it will call update_remote_idx with empty host
    // But the early return in on_master_record_write means nothing happens
    // Actually, on_master_record_write checks running_ and returns early
    EXPECT_FALSE(DataService::instance().has_remote_location(full_name));

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
    CMString db_id = "test_db_restore_existing";
    CMString base_path = tmpdir.path();

    // Create idx file with entries
    IndexEntry entry1;
    entry1.object_name = "obj_restore_1";
    entry1.file_name = "data_0.bin";
    entry1.offset = 0;
    entry1.size = 100;
    entry1.compression_type = 0;

    IndexEntry entry2;
    entry2.object_name = "obj_restore_2";
    entry2.file_name = "data_0.bin";
    entry2.offset = 100;
    entry2.size = 200;
    entry2.compression_type = 0;

    create_idx_file(base_path, "master000", {entry1, entry2});

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, "master000");

    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].object_name, "obj_restore_1");
    EXPECT_EQ(entries[1].object_name, "obj_restore_2");

    // DataService local_idx should be populated
    EXPECT_TRUE(DataService::instance().has_local_object("obj_restore_1"));
    EXPECT_TRUE(DataService::instance().has_local_object("obj_restore_2"));

    // Cleanup
    DataService::instance().remove_local_index("obj_restore_1");
    DataService::instance().remove_local_index("obj_restore_2");
}

TEST(MasterAgentTest, RestoreMasterIdx_NonExistentIdxFile) {
    TempDir tmpdir;
    CMString db_id = "test_db_restore_missing";

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, tmpdir.path(), "w999");

    EXPECT_TRUE(entries.empty());
}

TEST(MasterAgentTest, RestoreMasterIdx_EmptyIdxFile) {
    TempDir tmpdir;
    CMString db_id = "test_db_restore_empty";
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
    CMString db_id = "test_db_restore_multi";
    CMString base_path = tmpdir.path();

    // Create multiple entries
    CMVector<IndexEntry> entries_writer0;
    for (int i = 0; i < 5; i++) {
        IndexEntry e;
        e.object_name = fmt::format("multi_obj_{}", i);
        e.file_name = "data_0.bin";
        e.offset = i * 100;
        e.size = 100;
        e.compression_type = 0;
        entries_writer0.push_back(e);
    }
    create_idx_file(base_path, "master000", entries_writer0);

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, "master000");

    ASSERT_EQ(entries.size(), 5u);
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(entries[i].object_name, fmt::format("multi_obj_{}", i));
    }

    // Cleanup
    for (int i = 0; i < 5; i++) {
        DataService::instance().remove_local_index(fmt::format("multi_obj_{}", i));
    }
}

// --- rebuild_remote_idx ---

TEST(MasterAgentTest, RebuildRemoteIdx_MasterEntries) {
    TempDir tmpdir;
    CMString db_id = "test_db_rebuild_master";
    CMString base_path = tmpdir.path();

    // Create master's idx with entries
    IndexEntry entry;
    entry.object_name = "master_obj_1";
    entry.file_name = "data_0.bin";
    entry.offset = 0;
    entry.size = 50;
    entry.compression_type = 0;

    create_idx_file(base_path, "master000", {entry});

    // WorkerInfo for master (worker_id=0, hostname="localhost")
    ::WorkerInfo master_worker;
    master_worker.worker_id = 0;
    master_worker.writer_id = "master000";
    master_worker.hostname = "localhost";

    MasterAgent master("127.0.0.1", 0);
    // Register a new worker on "localhost" so master's entries get mapped to it
    DataService::instance().register_worker(10, "127.0.0.1", 9999);
    master.add_worker_hostname(10, "localhost");

    master.rebuild_remote_idx(db_id, base_path, {master_worker});

    // Master entries now map to the new worker on same hostname
    EXPECT_TRUE(DataService::instance().has_remote_location("master_obj_1"));
    auto info = DataService::instance().lookup_remote_idx("master_obj_1");
    EXPECT_EQ(info.worker_id, 10u);
    EXPECT_EQ(info.host, "127.0.0.1");
    EXPECT_EQ(info.port, 9999u);

    // Cleanup
    DataService::instance().remove_remote_index("master_obj_1");
}

TEST(MasterAgentTest, RebuildRemoteIdx_WorkerEntries_NoNewWorkers_Skipped) {
    TempDir tmpdir;
    CMString db_id = "test_db_rebuild_no_new_workers";
    CMString base_path = tmpdir.path();

    // Create worker_5.idx with entries
    IndexEntry entry;
    entry.object_name = "worker_obj_skip";
    entry.file_name = "data_5.bin";
    entry.offset = 0;
    entry.size = 50;
    entry.compression_type = 0;

    create_idx_file(base_path, "worker005", {entry});

    // WorkerInfo for old worker_id=5, but no new workers registered
    ::WorkerInfo old_worker;
    old_worker.worker_id = 5;
    old_worker.writer_id = "worker005";
    old_worker.hostname = "testhost_skipped";

    MasterAgent master("127.0.0.1", 0);
    master.rebuild_remote_idx(db_id, base_path, {old_worker});

    // No new workers with matching hostname → entry should NOT be in remote_idx
    EXPECT_FALSE(DataService::instance().has_remote_location("worker_obj_skip"));
}

TEST(MasterAgentTest, RebuildRemoteIdx_MissingIdxFile_Skipped) {
    TempDir tmpdir;
    CMString db_id = "test_db_rebuild_missing_idx";
    CMString base_path = tmpdir.path();

    // WorkerInfo for a worker whose idx file doesn't exist
    ::WorkerInfo missing_worker;
    missing_worker.worker_id = 42;
    missing_worker.writer_id = "worker042";
    missing_worker.hostname = "ghost_host";

    MasterAgent master("127.0.0.1", 0);
    // Should not crash, just WARN and skip
    master.rebuild_remote_idx(db_id, base_path, {missing_worker});
}

TEST(MasterAgentTest, RebuildRemoteIdx_MultipleWorkers) {
    TempDir tmpdir;
    CMString db_id = "test_db_rebuild_multi";
    CMString base_path = tmpdir.path();

    // Create master's idx
    IndexEntry master_entry;
    master_entry.object_name = "multi_master_obj";
    master_entry.file_name = "data_0.bin";
    master_entry.offset = 0;
    master_entry.size = 50;
    master_entry.compression_type = 0;
    create_idx_file(base_path, "master000", {master_entry});

    // Create worker's idx
    IndexEntry worker_entry;
    worker_entry.object_name = "multi_worker_obj";
    worker_entry.file_name = "data_3.bin";
    worker_entry.offset = 0;
    worker_entry.size = 100;
    worker_entry.compression_type = 0;
    create_idx_file(base_path, "worker003", {worker_entry});

    ::WorkerInfo w0;
    w0.worker_id = 0;
    w0.writer_id = "master000";
    w0.hostname = "master_host";

    ::WorkerInfo w3;
    w3.worker_id = 3;
    w3.writer_id = "worker003";
    w3.hostname = "unknown_host";

    MasterAgent master("127.0.0.1", 0);
    // Register a new worker on "master_host" for master's entries
    DataService::instance().register_worker(10, "127.0.0.1", 9999);
    master.add_worker_hostname(10, "master_host");

    master.rebuild_remote_idx(db_id, base_path, {w0, w3});

    // master entries → mapped to new worker_id=10 on "master_host"
    EXPECT_TRUE(DataService::instance().has_remote_location("multi_master_obj"));
    auto info0 = DataService::instance().lookup_remote_idx("multi_master_obj");
    EXPECT_EQ(info0.worker_id, 10u);
    EXPECT_EQ(info0.host, "127.0.0.1");
    EXPECT_EQ(info0.port, 9999u);

    // worker_id=3 entries → no matching new worker on "unknown_host" → skipped
    EXPECT_FALSE(DataService::instance().has_remote_location("multi_worker_obj"));

    // Cleanup
    DataService::instance().remove_remote_index("multi_master_obj");
}

TEST(MasterAgentTest, RebuildRemoteIdx_EmptyWorkers_Noop) {
    TempDir tmpdir;
    CMString db_id = "test_db_rebuild_empty_workers";
    CMString base_path = tmpdir.path();

    MasterAgent master("127.0.0.1", 0);
    // Empty workers vector → no iteration, no crash
    master.rebuild_remote_idx(db_id, base_path, {});
}

TEST(MasterAgentTest, RebuildRemoteIdx_MultiHost_MappedToCorrectWorkers) {
    TempDir tmpdir;
    CMString db_id = "test_db_multi_host";
    CMString base_path = tmpdir.path();

    // Master (worker_id=0) on "host_master"
    IndexEntry master_entry;
    master_entry.object_name = "master_data";
    master_entry.file_name = "data_m.bin";
    master_entry.offset = 0;
    master_entry.size = 50;
    master_entry.compression_type = 0;
    create_idx_file(base_path, "w_master", {master_entry});

    // Worker A (worker_id=1) on "host_a"
    IndexEntry worker_a_entry;
    worker_a_entry.object_name = "worker_a_data";
    worker_a_entry.file_name = "data_a.bin";
    worker_a_entry.offset = 0;
    worker_a_entry.size = 80;
    worker_a_entry.compression_type = 0;
    create_idx_file(base_path, "w_hosta", {worker_a_entry});

    // Worker B (worker_id=2) on "host_b"
    IndexEntry worker_b_entry;
    worker_b_entry.object_name = "worker_b_data";
    worker_b_entry.file_name = "data_b.bin";
    worker_b_entry.offset = 0;
    worker_b_entry.size = 120;
    worker_b_entry.compression_type = 0;
    create_idx_file(base_path, "w_hostb", {worker_b_entry});

    ::WorkerInfo w_master;
    w_master.worker_id = 0;
    w_master.writer_id = "w_master";
    w_master.hostname = "host_master";

    ::WorkerInfo w_a;
    w_a.worker_id = 1;
    w_a.writer_id = "w_hosta";
    w_a.hostname = "host_a";

    ::WorkerInfo w_b;
    w_b.worker_id = 2;
    w_b.writer_id = "w_hostb";
    w_b.hostname = "host_b";

    MasterAgent master("127.0.0.1", 0);

    DataService::instance().register_worker(100, "10.0.0.1", 8001);
    master.add_worker_hostname(100, "host_master");
    DataService::instance().register_worker(200, "10.0.0.2", 8002);
    master.add_worker_hostname(200, "host_a");
    DataService::instance().register_worker(300, "10.0.0.3", 8003);
    master.add_worker_hostname(300, "host_b");

    master.rebuild_remote_idx(db_id, base_path, {w_master, w_a, w_b});

    // master → worker 100 on host_master
    EXPECT_TRUE(DataService::instance().has_remote_location("master_data"));
    auto info_m = DataService::instance().lookup_remote_idx("master_data");
    EXPECT_EQ(info_m.worker_id, 100u);
    EXPECT_EQ(info_m.host, "10.0.0.1");
    EXPECT_EQ(info_m.port, 8001u);

    // worker A → worker 200 on host_a
    EXPECT_TRUE(DataService::instance().has_remote_location("worker_a_data"));
    auto info_a = DataService::instance().lookup_remote_idx("worker_a_data");
    EXPECT_EQ(info_a.worker_id, 200u);
    EXPECT_EQ(info_a.host, "10.0.0.2");
    EXPECT_EQ(info_a.port, 8002u);

    // worker B → worker 300 on host_b
    EXPECT_TRUE(DataService::instance().has_remote_location("worker_b_data"));
    auto info_b = DataService::instance().lookup_remote_idx("worker_b_data");
    EXPECT_EQ(info_b.worker_id, 300u);
    EXPECT_EQ(info_b.host, "10.0.0.3");
    EXPECT_EQ(info_b.port, 8003u);

    DataService::instance().remove_remote_index("master_data");
    DataService::instance().remove_remote_index("worker_a_data");
    DataService::instance().remove_remote_index("worker_b_data");
}

TEST(MasterAgentTest, RebuildRemoteIdx_SameHostMasterAndWorker_Merged) {
    TempDir tmpdir;
    CMString db_id = "test_db_same_host_merge";
    CMString base_path = tmpdir.path();

    // Master (worker_id=0) on "host_local"
    IndexEntry master_entry;
    master_entry.object_name = "m_obj";
    master_entry.file_name = "data_m.bin";
    master_entry.offset = 0;
    master_entry.size = 50;
    master_entry.compression_type = 0;
    create_idx_file(base_path, "w_m", {master_entry});

    // Worker (worker_id=5) on same "host_local"
    IndexEntry worker_entry;
    worker_entry.object_name = "w_obj";
    worker_entry.file_name = "data_w.bin";
    worker_entry.offset = 0;
    worker_entry.size = 80;
    worker_entry.compression_type = 0;
    create_idx_file(base_path, "w_w5", {worker_entry});

    // Remote Worker (worker_id=3) on "host_remote"
    IndexEntry remote_entry;
    remote_entry.object_name = "r_obj";
    remote_entry.file_name = "data_r.bin";
    remote_entry.offset = 0;
    remote_entry.size = 120;
    remote_entry.compression_type = 0;
    create_idx_file(base_path, "w_r3", {remote_entry});

    ::WorkerInfo w_m;
    w_m.worker_id = 0;
    w_m.writer_id = "w_m";
    w_m.hostname = "host_local";

    ::WorkerInfo w5;
    w5.worker_id = 5;
    w5.writer_id = "w_w5";
    w5.hostname = "host_local";

    ::WorkerInfo w3;
    w3.worker_id = 3;
    w3.writer_id = "w_r3";
    w3.hostname = "host_remote";

    MasterAgent master("127.0.0.1", 0);

    DataService::instance().register_worker(10, "192.168.1.1", 9001);
    master.add_worker_hostname(10, "host_local");
    DataService::instance().register_worker(20, "192.168.1.2", 9002);
    master.add_worker_hostname(20, "host_remote");

    master.rebuild_remote_idx(db_id, base_path, {w_m, w5, w3});

    // Both master and worker_id=5 on host_local → mapped to worker 10
    auto info_m = DataService::instance().lookup_remote_idx("m_obj");
    EXPECT_EQ(info_m.worker_id, 10u);
    EXPECT_EQ(info_m.host, "192.168.1.1");
    EXPECT_EQ(info_m.port, 9001u);

    auto info_w = DataService::instance().lookup_remote_idx("w_obj");
    EXPECT_EQ(info_w.worker_id, 10u);
    EXPECT_EQ(info_w.host, "192.168.1.1");
    EXPECT_EQ(info_w.port, 9001u);

    // Remote worker on host_remote → mapped to worker 20
    auto info_r = DataService::instance().lookup_remote_idx("r_obj");
    EXPECT_EQ(info_r.worker_id, 20u);
    EXPECT_EQ(info_r.host, "192.168.1.2");
    EXPECT_EQ(info_r.port, 9002u);

    DataService::instance().remove_remote_index("m_obj");
    DataService::instance().remove_remote_index("w_obj");
    DataService::instance().remove_remote_index("r_obj");
}

TEST(MasterAgentTest, RebuildRemoteIdx_PartialHostCoverage) {
    TempDir tmpdir;
    CMString db_id = "test_db_partial_host";
    CMString base_path = tmpdir.path();

    // Worker A on "host_available"
    IndexEntry entry_a;
    entry_a.object_name = "avail_obj";
    entry_a.file_name = "data_a.bin";
    entry_a.offset = 0;
    entry_a.size = 50;
    entry_a.compression_type = 0;
    create_idx_file(base_path, "w_avail", {entry_a});

    // Worker B on "host_offline"
    IndexEntry entry_b;
    entry_b.object_name = "offline_obj";
    entry_b.file_name = "data_b.bin";
    entry_b.offset = 0;
    entry_b.size = 80;
    entry_b.compression_type = 0;
    create_idx_file(base_path, "w_off", {entry_b});

    ::WorkerInfo wa;
    wa.worker_id = 1;
    wa.writer_id = "w_avail";
    wa.hostname = "host_available";

    ::WorkerInfo wb;
    wb.worker_id = 2;
    wb.writer_id = "w_off";
    wb.hostname = "host_offline";

    MasterAgent master("127.0.0.1", 0);

    // Only register a worker for "host_available", NOT for "host_offline"
    DataService::instance().register_worker(50, "10.0.0.10", 7000);
    master.add_worker_hostname(50, "host_available");

    master.rebuild_remote_idx(db_id, base_path, {wa, wb});

    // host_available → mapped
    EXPECT_TRUE(DataService::instance().has_remote_location("avail_obj"));
    auto info = DataService::instance().lookup_remote_idx("avail_obj");
    EXPECT_EQ(info.worker_id, 50u);

    // host_offline → skipped (no worker registered)
    EXPECT_FALSE(DataService::instance().has_remote_location("offline_obj"));

    DataService::instance().remove_remote_index("avail_obj");
}

}  // namespace fly