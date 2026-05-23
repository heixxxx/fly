#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <thread>
#include <chrono>

namespace fly {

TEST(MasterAgentTest, CreateAndStart) {
    MasterAgent master("127.0.0.1", 0);
    master.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(master.is_running());
    EXPECT_GT(master.get_port(), 0);
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
}

TEST(MasterAgentTest, CreateWithDifferentPorts) {
    MasterAgent master1("127.0.0.1", 0);
    MasterAgent master2("127.0.0.1", 0);
    
    master1.start();
    master2.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(master1.is_running());
    EXPECT_TRUE(master2.is_running());
    EXPECT_GT(master1.get_port(), 0);
    EXPECT_GT(master2.get_port(), 0);
    EXPECT_NE(master1.get_port(), master2.get_port());
    
    master1.stop();
    master2.stop();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_FALSE(master1.is_running());
    EXPECT_FALSE(master2.is_running());
}

TEST(MasterAgentTest, MultipleStartStop) {
    MasterAgent master("127.0.0.1", 0);
    
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(master.is_running());
    
    master.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(master.is_running());
    
    master.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

void create_idx_file(const CMString& base_path, uint64_t writer_id,
                     const CMVector<IndexEntry>& entries) {
    CMString idx_path = base_path + "/worker_" + ::std::to_string(writer_id) + ".idx";
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
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

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
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

    create_idx_file(base_path, 0, {entry1, entry2});

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, 0);

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
    auto entries = master.restore_master_idx(db_id, tmpdir.path(), 999);

    EXPECT_TRUE(entries.empty());
}

TEST(MasterAgentTest, RestoreMasterIdx_EmptyIdxFile) {
    TempDir tmpdir;
    CMString db_id = "test_db_restore_empty";
    CMString base_path = tmpdir.path();

    // Create idx file with no entries
    CMString idx_path = base_path + "/worker_0.idx";
    {
        // LocalIndex with no entries → save writes nothing (not modified)
        // So we need to touch the file manually to create an empty file
        std::ofstream ofs(idx_path, std::ios::binary);
        // Empty file
    }

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, 0);

    EXPECT_TRUE(entries.empty());
}

TEST(MasterAgentTest, RestoreMasterIdx_MultipleEntries) {
    TempDir tmpdir;
    CMString db_id = "test_db_restore_multi";
    CMString base_path = tmpdir.path();

    // Create multiple entries with different writer_id
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
    create_idx_file(base_path, 0, entries_writer0);

    MasterAgent master("127.0.0.1", 0);
    auto entries = master.restore_master_idx(db_id, base_path, 0);

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

    // Create worker_0.idx with entries
    IndexEntry entry;
    entry.object_name = "master_obj_1";
    entry.file_name = "data_0.bin";
    entry.offset = 0;
    entry.size = 50;
    entry.compression_type = 0;

    create_idx_file(base_path, 0, {entry});

    // WorkerInfo for master (worker_id=0)
    ::WorkerInfo master_worker;
    master_worker.worker_id = 0;
    master_worker.hostname = "localhost";

    MasterAgent master("127.0.0.1", 0);
    master.rebuild_remote_idx(db_id, base_path, {master_worker});

    // For worker_id=0, maps to host_ and data_server_port_
    EXPECT_TRUE(DataService::instance().has_remote_location("master_obj_1"));
    auto info = DataService::instance().lookup_remote_idx("master_obj_1");
    EXPECT_EQ(info.worker_id, 0u);
    EXPECT_EQ(info.host, "127.0.0.1");

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

    create_idx_file(base_path, 5, {entry});

    // WorkerInfo for old worker_id=5, but no new workers registered
    ::WorkerInfo old_worker;
    old_worker.worker_id = 5;
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
    missing_worker.hostname = "ghost_host";

    MasterAgent master("127.0.0.1", 0);
    // Should not crash, just WARN and skip
    master.rebuild_remote_idx(db_id, base_path, {missing_worker});
}

TEST(MasterAgentTest, RebuildRemoteIdx_MultipleWorkers) {
    TempDir tmpdir;
    CMString db_id = "test_db_rebuild_multi";
    CMString base_path = tmpdir.path();

    // Create worker_0.idx
    IndexEntry master_entry;
    master_entry.object_name = "multi_master_obj";
    master_entry.file_name = "data_0.bin";
    master_entry.offset = 0;
    master_entry.size = 50;
    master_entry.compression_type = 0;
    create_idx_file(base_path, 0, {master_entry});

    // Create worker_3.idx
    IndexEntry worker_entry;
    worker_entry.object_name = "multi_worker_obj";
    worker_entry.file_name = "data_3.bin";
    worker_entry.offset = 0;
    worker_entry.size = 100;
    worker_entry.compression_type = 0;
    create_idx_file(base_path, 3, {worker_entry});

    // WorkerInfo vector
    ::WorkerInfo w0;
    w0.worker_id = 0;
    w0.hostname = "master_host";

    ::WorkerInfo w3;
    w3.worker_id = 3;
    w3.hostname = "unknown_host";

    MasterAgent master("127.0.0.1", 0);
    master.rebuild_remote_idx(db_id, base_path, {w0, w3});

    // worker_0 entries → mapped to master
    EXPECT_TRUE(DataService::instance().has_remote_location("multi_master_obj"));
    auto info0 = DataService::instance().lookup_remote_idx("multi_master_obj");
    EXPECT_EQ(info0.worker_id, 0u);
    EXPECT_EQ(info0.host, "127.0.0.1");

    // worker_3 entries → no matching new worker → skipped
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

}  // namespace fly