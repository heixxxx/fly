#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <network/cpp/net_quality_monitor.h>
#include <common/cpp/fly_buffer.h>
#include <filesystem>
#include <istream>
#include <chrono>

namespace {

static void write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
}

// db_path 废弃：db_path 现在是 db_path 的别名（不含 ':'，否则 split 歧义）。
// db32() 生成不含 ':' 的 db_path 用于测试。保留函数名 db32 仅为调用点兼容。
static CMString db32(const CMString& hint) {
    // 用路径风格（含 '/' 但不含 ':'），模拟真实 db_path。
    return "/test/" + hint;
}

class DataServiceTest : public ::testing::Test {
protected:
    CMString test_dir_;
    fly::CMSharedPtr<fly::DataService> ds_ = fly::DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_ds_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
        // DataService is a process-wide singleton shared across tests. Prior
        // tests may have left handler lambdas (capturing local references) and
        // index state behind; without a reset a later test that walks TIER2/TIER3
        // can invoke a dangling lambda → use-after-free. reset() clears the
        // handlers and all indexes so every test starts from a clean slate.
        ds_->reset();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DataServiceTest, TryReadLocalReturnsFalseForUnknownObject) {
    auto [found, result] = ds_->try_read_local("no_such_object");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, HasLocalObjectReturnsFalseWhenEmpty) {
    EXPECT_FALSE(ds_->has_local_object("anything"));
}

TEST_F(DataServiceTest, OnObjectWrittenAndFlushEnablesLocalRead) {
    CMString db_path = test_dir_ + "/local_read";
    Database db(db_path);

    write_raw(db, "local/obj", "hello", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("local/obj");
    EXPECT_TRUE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "hello");
}

TEST_F(DataServiceTest, IncompleteObjectNotReadable) {
    CMString db_path = db32("test_db");
    CMString full = db_path + ":pending/obj";

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, WriteCompletedMarksObjectReadable) {
    CMString db_path = db32("flush_db");
    CMString full = db_path + ":flush/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 10;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));

    CMVector<IndexEntry> entries = {entry};
    ds_->on_write_completed(db_path, full, entries);
    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, UpdateRemoteIdxAndLookup) {
    CMString full = db32("remote") + ":obj";
    EXPECT_FALSE(ds_->has_remote_location(full));

    ds_->update_remote_idx(full, 42, "192.168.1.10", 9000);

    EXPECT_TRUE(ds_->has_remote_location(full));

    auto info = ds_->lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id_, 42u);
    EXPECT_EQ(info.host_, "192.168.1.10");
    EXPECT_EQ(info.port_, 9000);
}

TEST_F(DataServiceTest, LookupRemoteIdxReturnsEmptyForUnknown) {
    auto info = ds_->lookup_remote_idx("unknown/obj");
    EXPECT_EQ(info.worker_id_, 0u);
    EXPECT_TRUE(info.host_.empty());
    EXPECT_EQ(info.port_, 0);
}

TEST_F(DataServiceTest, RegisterWorkerAndGetAddress) {
    ds_->register_worker(1, "10.0.0.1", 8001);
    ds_->register_worker(2, "10.0.0.2", 8002);

    auto addr1 = ds_->get_worker_address(1);
    EXPECT_EQ(addr1.host_, "10.0.0.1");
    EXPECT_EQ(addr1.port_, 8001);

    auto addr2 = ds_->get_worker_address(2);
    EXPECT_EQ(addr2.host_, "10.0.0.2");
    EXPECT_EQ(addr2.port_, 8002);
}

// get_all_workers returns a detached snapshot of every registered data-server
// peer. The bandwidth-probe thread uses it to pick its targets.
TEST_F(DataServiceTest, GetAllWorkersReturnsSnapshot) {
    ds_->register_worker(9101, "probe_a", 9101);
    ds_->register_worker(9102, "probe_b", 9102);

    auto peers = ds_->get_all_workers();
    bool has_a = false, has_b = false;
    for (const auto& p : peers) {
        if (p.worker_id_ == 9101) { has_a = true; EXPECT_EQ(p.host_, "probe_a"); }
        if (p.worker_id_ == 9102) { has_b = true; EXPECT_EQ(p.host_, "probe_b"); }
    }
    EXPECT_TRUE(has_a);
    EXPECT_TRUE(has_b);
}

TEST_F(DataServiceTest, GetWorkerAddressReturnsEmptyForUnknown) {
    auto addr = ds_->get_worker_address(9999);
    EXPECT_EQ(addr.worker_id_, 0u);
    EXPECT_TRUE(addr.host_.empty());
}

TEST_F(DataServiceTest, MultipleObjectsInSameDatabase) {
    CMString db_path = test_dir_ + "/multi_obj";
    Database db(db_path);

    write_raw(db, "multi/a", "data_a", false);
    write_raw(db, "multi/b", "data_b", false);
    write_raw(db, "multi/c", "data_c", false);
    fly::DataService::instance()->drain_write_back();

    EXPECT_TRUE(ds_->has_local_object(db.get_full_name("multi/a")));
    EXPECT_TRUE(ds_->has_local_object(db.get_full_name("multi/b")));
    EXPECT_TRUE(ds_->has_local_object(db.get_full_name("multi/c")));

    auto [fa, ra] = ds_->try_read_local(db.get_full_name("multi/a"));
    EXPECT_TRUE(fa);
    CMString da(ra.data_buffer_.begin(), ra.data_buffer_.end());
    EXPECT_EQ(da, "data_a");

    auto [fb, rb] = ds_->try_read_local(db.get_full_name("multi/b"));
    EXPECT_TRUE(fb);
    CMString db2(rb.data_buffer_.begin(), rb.data_buffer_.end());
    EXPECT_EQ(db2, "data_b");
}

TEST_F(DataServiceTest, TypedObjectReadableViaDataService) {
    CMString db_path = test_dir_ + "/typed_ds";
    Database db(db_path);

    db.write_pickle_bytes("typed/ds_obj", "typed_payload", 13, "MyType");
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("typed/ds_obj");
    auto [found, result] = ds_->try_read_local(full);
    EXPECT_TRUE(found);
    EXPECT_EQ(result.py_name_, "MyType");
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "typed_payload");
}

TEST_F(DataServiceTest, RemoteIdxSupportsMultipleWorkers) {
    CMString full = db32("multi") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    // Both workers should be tracked
    auto workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 2u);
    EXPECT_EQ(workers[0], 1u);
    EXPECT_EQ(workers[1], 2u);

    // Lookup returns first registered worker
    auto info = ds_->lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id_, 1u);
    EXPECT_EQ(info.host_, "host_a");
    EXPECT_EQ(info.port_, 8000);
}

TEST_F(DataServiceTest, LookupAllRemoteIdxReturnsEveryReplica) {
    CMString full = db32("allrepl") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);
    ds_->update_remote_idx(full, 3, "host_c", 7000);

    // Must return ALL replicas (worker + host + port), not just the first.
    auto all = ds_->lookup_all_remote_idx(full);
    EXPECT_EQ(all.size(), 3u);

    // Each replica's address must resolve via worker_registry_.
    std::set<uint64_t> seen_wids;
    for (const auto& loc : all) {
        EXPECT_FALSE(loc.host_.empty());
        seen_wids.insert(loc.worker_id_);
    }
    EXPECT_EQ(seen_wids.size(), 3u);
}

TEST_F(DataServiceTest, LookupAllRemoteIdxEmptyForUnknownObject) {
    auto all = ds_->lookup_all_remote_idx(db32("none") + ":missing");
    EXPECT_TRUE(all.empty());
}

TEST_F(DataServiceTest, RegisterDatabaseAndLocalRead) {
    CMString db_path = test_dir_ + "/reg_db";
    std::filesystem::create_directories(db_path);

    ds_->register_database(db_path, "");

    CMString full = db_path + ":manual/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);

    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, RemoveLocalIndexMakesObjectUnreadable) {
    CMString db_path = test_dir_ + "/remove_local";
    Database db(db_path);

    write_raw(db, "remove/local", "data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("remove/local");
    EXPECT_TRUE(ds_->has_local_object(full));

    ds_->remove_local_index(full);

    EXPECT_FALSE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, RemoveLocalIndexOnlyAffectsTarget) {
    CMString db_path = test_dir_ + "/remove_one_local";
    Database db(db_path);

    write_raw(db, "keep/me", "keep_data", false);
    write_raw(db, "remove/me", "remove_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString keep_full = db.get_full_name("keep/me");
    CMString remove_full = db.get_full_name("remove/me");

    ds_->remove_local_index(remove_full);

    EXPECT_TRUE(ds_->has_local_object(keep_full));
    EXPECT_FALSE(ds_->has_local_object(remove_full));
}

TEST_F(DataServiceTest, RemoveRemoteIndexClearsLocation) {
    CMString full = db32("remote") + ":remove_test";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    EXPECT_TRUE(ds_->has_remote_location(full));

    ds_->remove_remote_index(full);

    EXPECT_FALSE(ds_->has_remote_location(full));

    auto info = ds_->lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id_, 0u);
    EXPECT_TRUE(info.host_.empty());
}

TEST_F(DataServiceTest, RemoveRemoteIndexOnlyAffectsTarget) {
    CMString keep_full = db32("remote") + ":keep";
    CMString remove_full = db32("remote") + ":remove";
    ds_->update_remote_idx(keep_full, 1, "host_a", 8000);
    ds_->update_remote_idx(remove_full, 2, "host_b", 9000);

    ds_->remove_remote_index(remove_full);

    EXPECT_TRUE(ds_->has_remote_location(keep_full));
    EXPECT_FALSE(ds_->has_remote_location(remove_full));
}

TEST_F(DataServiceTest, RestoreEntriesMakesObjectsReadable) {
    CMString db_path = test_dir_ + "/restore_test";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name_ = db_path + ":obj_a";
    e1.file_name_ = "test.dat";
    e1.offset_ = 0;
    e1.size_ = 5;
    e1.is_large_ = false;
    e1.block_count_ = 0;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name_ = db_path + ":obj_b";
    e2.file_name_ = "test.dat";
    e2.offset_ = 10;
    e2.size_ = 7;
    e2.is_large_ = false;
    e2.block_count_ = 0;
    entries.push_back(e2);

    ds_->restore_entries(db_path, entries);

    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj_a"));
    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj_b"));
}

TEST_F(DataServiceTest, RestoreEntriesMultipleEntriesPerObject) {
    CMString db_path = test_dir_ + "/restore_multi";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name_ = db_path + ":large_obj";
    e1.file_name_ = "data_0.dat";
    e1.offset_ = 0;
    e1.size_ = 100;
    e1.is_large_ = true;
    e1.block_count_ = 1;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name_ = db_path + ":large_obj";
    e2.file_name_ = "data_0.dat";
    e2.offset_ = 100;
    e2.size_ = 200;
    e2.is_large_ = true;
    e2.block_count_ = 1;
    entries.push_back(e2);

    ds_->restore_entries(db_path, entries);

    EXPECT_TRUE(ds_->has_local_object(db_path + ":large_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesAppendsToExisting) {
    CMString db_path = test_dir_ + "/restore_append";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    IndexEntry e1;
    e1.object_name_ = db_path + ":obj";
    e1.file_name_ = "test.dat";
    e1.offset_ = 0;
    e1.size_ = 5;
    e1.is_large_ = false;
    e1.block_count_ = 0;
    ds_->on_object_written(db_path, db_path + ":obj", e1);
    ds_->on_flush(db_path);
    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj"));

    CMVector<IndexEntry> restore_entries_vec;
    IndexEntry e2;
    e2.object_name_ = db_path + ":new_obj";
    e2.file_name_ = "test.dat";
    e2.offset_ = 100;
    e2.size_ = 10;
    e2.is_large_ = false;
    e2.block_count_ = 0;
    restore_entries_vec.push_back(e2);

    ds_->restore_entries(db_path, restore_entries_vec);

    EXPECT_TRUE(ds_->has_local_object(db_path + ":obj"));
    EXPECT_TRUE(ds_->has_local_object(db_path + ":new_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesEmptyVectorIsNoop) {
    CMVector<IndexEntry> empty;
    ds_->restore_entries(db32("empty_db"), empty);
}

TEST_F(DataServiceTest, RestoreEntriesFromLocalIndexFile) {
    CMString db_path = test_dir_ + "/restore_from_idx";
    Database db(db_path);

    write_raw(db, "idx/obj1", "data1", false);
    write_raw(db, "idx/obj2", "data2", false);
    fly::DataService::instance()->drain_write_back();

    CMString idx_path = db_path + "/" + db.get_writer_id() + ".idx";

    LocalIndex source_idx(idx_path);
    source_idx.load();
    auto all_entries = source_idx.get_all_entries();
    ASSERT_FALSE(all_entries.empty());

    CMString original_db_path = db.get_db_path();
    CMString restore_base = test_dir_ + "/restored_db";
    std::filesystem::create_directories(restore_base);
    ds_->register_database(original_db_path, restore_base, "");

    ds_->restore_entries(original_db_path, all_entries);

    // entry.object_name_ 是 short_name（LocalIndex 不再存 db_path 前缀），
    // has_local_object 用 full_name 作 key，需拼接。
    for (const auto& e : all_entries) {
        EXPECT_TRUE(ds_->has_local_object(original_db_path + ":" + e.object_name_));
    }
}

TEST_F(DataServiceTest, DbPathWithSlashesHandledCorrectly) {
    // db_path 废弃后 db_path == db_path（含 '/'，不含 ':'）。
    // full_name = "db_path:short"，split 用 rfind(':') 正确切分。
    CMString db_path = db32("path_test");  // "/test/path_test"
    CMString full = db_path + ":obj/with/slashes";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);

    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, SplitFullRfindHandlesLongDbPath) {
    // db_path 变长（不再是固定 10 字符），rfind(':') 必须正确切分。
    // 验证 split_full_name 对长 db_path + short 的切分。
    auto [db, short_name] = fly::split_full_name("/a/very/long/db/path:my_obj");
    EXPECT_EQ(db, "/a/very/long/db/path");
    EXPECT_EQ(short_name, "my_obj");
}

TEST_F(DataServiceTest, ShortFullNameTreatedAsNoDbId) {
    CMString short_name = "short_obj_name";

    IndexEntry entry;
    entry.object_name_ = short_name;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written("", short_name, entry);
    ds_->on_flush("");

    EXPECT_TRUE(ds_->has_local_object(short_name));
}

TEST_F(DataServiceTest, RemoveIndexByShortName) {
    CMString db_path = db32("remove_short");
    CMString full = db_path + ":remove_target";

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);
    EXPECT_TRUE(ds_->has_local_object(full));

    ds_->remove_local_index(full);
    EXPECT_FALSE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, WriteCompletedOnlyAffectsTargetDb) {
    CMString db_a = db32("flush_db_a");
    CMString db_b = db32("flush_db_b");
    CMString full_a = db_a + ":obj_a";
    CMString full_b = db_b + ":obj_b";

    IndexEntry ea;
    ea.object_name_ = full_a;
    ea.file_name_ = "test.dat";
    ea.offset_ = 0;
    ea.size_ = 5;
    ea.is_large_ = false;
    ea.block_count_ = 0;

    IndexEntry eb;
    eb.object_name_ = full_b;
    eb.file_name_ = "test.dat";
    eb.offset_ = 0;
    eb.size_ = 5;
    eb.is_large_ = false;
    eb.block_count_ = 0;

    ds_->on_write_started(db_a, full_a);
    ds_->on_write_started(db_b, full_b);

    CMVector<IndexEntry> entries_a = {ea};
    ds_->on_write_completed(db_a, full_a, entries_a);
    EXPECT_TRUE(ds_->has_local_object(full_a));
    EXPECT_FALSE(ds_->has_local_object(full_b));

    CMVector<IndexEntry> entries_b = {eb};
    ds_->on_write_completed(db_b, full_b, entries_b);
    EXPECT_TRUE(ds_->has_local_object(full_b));
}

TEST_F(DataServiceTest, HasDatabaseReturnsTrue) {
    CMString db_path = test_dir_ + "/has_db";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");
    EXPECT_TRUE(ds_->has_database(db_path));
}

TEST_F(DataServiceTest, HasDatabaseReturnsFalseForUnknown) {
    EXPECT_FALSE(ds_->has_database(db32("unknown_db")));
}

TEST_F(DataServiceTest, UnregisterDatabaseRemovesIt) {
    CMString db_path = test_dir_ + "/unreg_db";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");
    EXPECT_TRUE(ds_->has_database(db_path));

    ds_->unregister_database(db_path);
    EXPECT_FALSE(ds_->has_database(db_path));
}

TEST_F(DataServiceTest, RemoveRemoteLocationByWorkerId) {
    CMString full = db32("rr_worker") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    auto workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 2u);

    ds_->remove_remote_location(full, 1);

    workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 2u);
}

TEST_F(DataServiceTest, RemoveRemoteLocationByWorkerIdCleansUpWhenEmpty) {
    CMString full = db32("rr_cleanup") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);

    ds_->remove_remote_location(full, 1);

    EXPECT_FALSE(ds_->has_remote_location(full));
    EXPECT_TRUE(ds_->get_remote_workers(full).empty());
}

TEST_F(DataServiceTest, OnObjectWrittenSetsComplete) {
    CMString db_path = db32("flush_obj_db");
    CMString full = db_path + ":flush/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteStartedCreatesEntry) {
    CMString db_path = db32("start_db");
    CMString full = db_path + ":started/obj";

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteFailedRemovesEntry) {
    CMString db_path = db32("fail_db");
    CMString full = db_path + ":failed/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);
    EXPECT_TRUE(ds_->has_local_object(full));

    ds_->on_write_failed(db_path, full, "error msg");
    EXPECT_FALSE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, AddRemoteLocation) {
    CMString full = db32("add_remote") + ":obj";
    ds_->register_worker(1, "host_a", 8000);
    ds_->add_remote_location(full, 1);

    EXPECT_TRUE(ds_->has_remote_location(full));
    auto workers = ds_->get_remote_workers(full);
    EXPECT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 1u);
}

TEST_F(DataServiceTest, GetRemoteWorkersEmptyForMissing) {
    auto workers = ds_->get_remote_workers("no/such/obj");
    EXPECT_TRUE(workers.empty());
}

TEST_F(DataServiceTest, HasRemoteLocationFalseForMissing) {
    EXPECT_FALSE(ds_->has_remote_location("missing/remote"));
}

TEST_F(DataServiceTest, WriteBackQueueStartStop) {
    ds_->stop_write_back();
    EXPECT_FALSE(ds_->is_write_back_running());

    ds_->start_write_back();
    EXPECT_TRUE(ds_->is_write_back_running());

    ds_->stop_write_back();
    EXPECT_FALSE(ds_->is_write_back_running());
}

TEST_F(DataServiceTest, FindLocalEntriesReturnsNoneForMissing) {
    auto entries = ds_->find_local_entries("no/local/entries");
    EXPECT_FALSE(entries.has_value());
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsFalseForMissingDb) {
    auto [found, result] = ds_->try_read_local_or_wait("no_such_object", 100);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsFalseForMissingEntry) {
    CMString db_path = db32("wait_missing");
    auto [found, result] = ds_->try_read_local_or_wait(db_path + ":no_entry", 100);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsImmediatelyWhenComplete) {
    CMString db_path = test_dir_ + "/wait_read";
    Database db(db_path);

    write_raw(db, "wait/obj", "wait_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("wait/obj");
    auto [found, result] = ds_->try_read_local_or_wait(full, 100);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "wait_data");
}

TEST_F(DataServiceTest, TryReadLocalOrWaitTimeoutOnIncomplete) {
    CMString db_path = db32("wait_timeout");
    CMString full = db_path + ":pending_obj";

    ds_->on_write_started(db_path, full);

    auto [found, result] = ds_->try_read_local_or_wait(full, 50);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsFalseOnFailed) {
    CMString db_path = db32("wait_fail");
    CMString full = db_path + ":fail_obj";

    ds_->on_write_started(db_path, full);
    ds_->on_write_failed(db_path, full, "test error");

    auto [found, result] = ds_->try_read_local_or_wait(full, 100);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalRawReturnsData) {
    CMString db_path = test_dir_ + "/raw_read";
    Database db(db_path);

    write_raw(db, "raw/obj", "raw_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("raw/obj");
    auto [found, raw] = ds_->try_read_local_raw(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!raw || raw->empty());
}

TEST_F(DataServiceTest, TryReadLocalRawReturnsFalseForMissing) {
    auto [found, raw] = ds_->try_read_local_raw("missing/obj");
    EXPECT_FALSE(found);
    EXPECT_TRUE(!raw);
}

TEST_F(DataServiceTest, TryReadLocalRawOrWaitReturnsData) {
    CMString db_path = test_dir_ + "/raw_wait";
    Database db(db_path);

    write_raw(db, "rawwait/obj", "rawwait_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("rawwait/obj");
    auto [found, raw, py_name] = ds_->try_read_local_raw_or_wait(full, 100);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!raw || raw->empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DataServiceTest, TryReadLocalRawOrWaitReturnsFalseForMissing) {
    auto [found, raw, py_name] = ds_->try_read_local_raw_or_wait("missing_raw/obj", 50);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalRawOrWaitTimeoutOnIncomplete) {
    CMString db_path = db32("raw_timeout");
    CMString full = db_path + ":incomplete_raw";
    ds_->on_write_started(db_path, full);

    auto [found, raw, py_name] = ds_->try_read_local_raw_or_wait(full, 50);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadRemoteReturnsLocalIfAvailable) {
    CMString db_path = test_dir_ + "/remote_local";
    Database db(db_path);

    write_raw(db, "remote/local_obj", "local_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("remote/local_obj");
    auto [found, result] = ds_->try_read_remote(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "local_data");
}

TEST_F(DataServiceTest, TryReadRemoteReturnsFalseForMissing) {
    auto [found, result] = ds_->try_read_remote("no_such_remote_obj");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadRemoteSetsCanStillProduceFlag) {
    auto [found, result] = ds_->try_read_remote("missing_remote_data");
    EXPECT_FALSE(found);
    EXPECT_FALSE(result.can_still_produce_);
}

TEST_F(DataServiceTest, ReadRawCompressedReturnsLocalRaw) {
    CMString db_path = test_dir_ + "/raw_comp";
    Database db(db_path);

    write_raw(db, "comp/obj", "comp_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("comp/obj");
    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!raw || raw->empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DataServiceTest, ReadRawCompressedReturnsFalseForMissing) {
    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed("missing_comp");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, RegisterDatabaseDuplicateUpdatesExisting) {
    CMString db_path = db32("dup_db");
    CMString base1 = test_dir_ + "/dup_db1";
    CMString base2 = test_dir_ + "/dup_db2";
    std::filesystem::create_directories(base1);
    std::filesystem::create_directories(base2);

    ds_->register_database(db_path, base1, "");
    EXPECT_TRUE(ds_->has_database(db_path));

    ds_->register_database(db_path, base2, "");
    EXPECT_TRUE(ds_->has_database(db_path));
}

TEST_F(DataServiceTest, RemoveRemoteLocationByFullObject) {
    CMString full = db32("rr_full") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    EXPECT_TRUE(ds_->has_remote_location(full));

    ds_->remove_remote_location(full);
    EXPECT_FALSE(ds_->has_remote_location(full));
}

TEST_F(DataServiceTest, RestoreEntriesWithShortObjectNames) {
    CMString db_path = test_dir_ + "/short_restore";
    std::filesystem::create_directories(db_path);
    ds_->register_database(db_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e;
    e.object_name_ = db_path + ":simple_name";
    e.file_name_ = "test.dat";
    e.offset_ = 0;
    e.size_ = 10;
    e.is_large_ = false;
    e.block_count_ = 0;
    entries.push_back(e);

    ds_->restore_entries(db_path, entries);
    EXPECT_TRUE(ds_->has_local_object(db_path + ":simple_name"));
}

TEST_F(DataServiceTest, OnWriteStartedAndCompletedCycle) {
    CMString db_path = db32("cycle_db");
    CMString full = db_path + ":cycle/obj";

    ds_->on_write_started(db_path, full);
    EXPECT_FALSE(ds_->has_local_object(full));

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 10;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    CMVector<IndexEntry> entries = {entry};
    ds_->on_write_completed(db_path, full, entries);
    ds_->on_object_flushed(full);

    EXPECT_TRUE(ds_->has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteCompletedForMissingDbIsNoop) {
    CMString db_path = db32("missing_db_wc");
    CMString full = db_path + ":missing/obj";
    CMVector<IndexEntry> entries;
    EXPECT_NO_THROW(ds_->on_write_completed(db_path, full, entries));
}

TEST_F(DataServiceTest, OnWriteFailedForMissingDbIsNoop) {
    CMString db_path = db32("missing_db_wf");
    CMString full = db_path + ":missing/obj";
    EXPECT_NO_THROW(ds_->on_write_failed(db_path, full, "error"));
}

TEST_F(DataServiceTest, OnFlushForMissingDbIsNoop) {
    EXPECT_NO_THROW(ds_->on_flush(db32("nonexistent_flush")));
}

TEST_F(DataServiceTest, FindLocalEntriesReturnsData) {
    CMString db_path = db32("find_db");
    CMString full = db_path + ":find/obj";
    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 10;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    ds_->on_object_written(db_path, full, entry);
    ds_->on_flush(db_path);

    auto found = ds_->find_local_entries(full);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->size(), 1u);
    EXPECT_EQ((*found)[0].object_name_, full);
}

TEST_F(DataServiceTest, SetRemoteCompressedReadHandler) {
    bool called = false;
    ds_->set_remote_compressed_read_handler([&called](const CMString& name) -> std::tuple<bool, bool> {
        called = true;
        return std::make_tuple(false, false);
    });
    EXPECT_NO_THROW(ds_->set_remote_compressed_read_handler(nullptr));
}

TEST_F(DataServiceTest, SetDirectCompressedReadHandler) {
    ds_->set_direct_compressed_read_handler(
        [](const CMString& host, int32_t port, const CMString& name) -> std::tuple<bool, FlyBufferPtr, CMString, CMString, fly::ReadError> {
            return std::make_tuple(false, nullptr, CMString{}, CMString{}, fly::ReadError::NETWORK);
        });
    EXPECT_NO_THROW(ds_->set_direct_compressed_read_handler(nullptr));
}

// TIER2 multi-replica failover: when the first replica returns a typed error,
// read_raw_compressed must move on to the next replica instead of giving up.
TEST_F(DataServiceTest, Tier2FailoverToNextReplicaOnObjectNotFound) {
    CMString full = db32("tier2") + ":obj";
    // Two replicas: W1 (will return OBJECT_NOT_FOUND) and W2 (has data).
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    // Build a tiny valid compressed payload so W2 "returns data".
    std::string payload = "hello";
    fly::CMSharedPtr<FlyBuffer> data = fly::CMMakeShared<FlyBuffer>();
    data->write(payload.data(), payload.size());

    int call_count = 0;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t port, const CMString& name)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            call_count++;
            if (host == "host_a") {
                // W1 no longer holds the object → permanent for this replica.
                return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
            }
            // W2 returns the data.
            return {true, data, CMString("bytes"), {}, fly::ReadError::NONE};
        });

    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_TRUE(found);
    ASSERT_TRUE(raw && !raw->empty());
    EXPECT_EQ(call_count, 2);  // tried W1, then W2
}

// TIER2 must remove a replica that returned OBJECT_NOT_FOUND, so a subsequent
// read goes straight to the surviving replica.
TEST_F(DataServiceTest, Tier2RemovesReplicaOnObjectNotFound) {
    CMString full = db32("tier2rm") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    fly::CMSharedPtr<FlyBuffer> data = fly::CMMakeShared<FlyBuffer>();
    data->write("x", 1);

    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t port, const CMString& name)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            if (host == "host_a") {
                return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
            }
            return {true, data, CMString("bytes"), {}, fly::ReadError::NONE};
        });

    auto [found, raw, py_name, hash, can_still] = ds_->read_raw_compressed(full);
    EXPECT_TRUE(found);

    // W1 must have been pruned: only W2 remains.
    auto workers = ds_->get_remote_workers(full);
    ASSERT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 2u);
}

TEST_F(DataServiceTest, DataServerStartStop) {
    ds_->start_data_server("127.0.0.1", 0, 1);
    EXPECT_GT(ds_->get_data_port(), 0);
    ds_->stop_data_server();
}

TEST_F(DataServiceTest, EnqueueWriteBackAutoStarts) {
    ds_->stop_write_back();
    EXPECT_FALSE(ds_->is_write_back_running());

    fly::WriteRequest req;
    req.execute_ = []() {};
    req.on_complete_ = []() {};
    ds_->enqueue_write_back(std::move(req));

    EXPECT_TRUE(ds_->is_write_back_running());

    ds_->drain_write_back();
    ds_->stop_write_back();
}

TEST_F(DataServiceTest, ResetClearsAllState) {
    CMString db_path = db32("reset_db");
    ds_->register_database(db_path, test_dir_, "");
    ds_->register_worker(1, "host", 8000);

    ds_->reset();

    EXPECT_FALSE(ds_->has_database(db_path));
}

// ============================================================
// Auto-Backup Access Tracking (inline in remote_idx_)
// ============================================================

TEST_F(DataServiceTest, RemoteObjectMetaTracksReadCount) {
    CMString full = db32("meta_test") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);
    
    // Access tracking is now on remote_idx_ directly
    ds_->record_remote_access(full);
    ds_->record_remote_access(full);
    
    EXPECT_EQ(ds_->get_access_read_count(full), 2u);
}

TEST_F(DataServiceTest, RemoteObjectMetaEvaluateAutoBackup) {
    CMString full = db32("eval_test") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);
    
    for (int i = 0; i < 5; i++) {
        ds_->record_remote_access(full);
    }
    
    // 1 worker, threshold=3, target=2 → should_backup
    auto decision = ds_->evaluate_auto_backup(full, 3, 2);
    EXPECT_TRUE(decision.should_backup_);
    EXPECT_EQ(decision.current_replicas_, 1u);
    EXPECT_EQ(decision.target_replicas_, 2u);
}

TEST_F(DataServiceTest, RemoteObjectMetaEvaluateSatisfied) {
    CMString full = db32("sat_test") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);
    ds_->update_remote_idx(full, 2, "host2", 1235);
    
    for (int i = 0; i < 5; i++) {
        ds_->record_remote_access(full);
    }
    
    // 2 workers already, target=2 → NOT should_backup
    auto decision = ds_->evaluate_auto_backup(full, 3, 2);
    EXPECT_FALSE(decision.should_backup_);
    EXPECT_EQ(decision.current_replicas_, 2u);
}

TEST_F(DataServiceTest, RemoteObjectMetaDecay) {
    CMString full = db32("decay_test") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);
    
    for (int i = 0; i < 10; i++) {
        ds_->record_remote_access(full);
    }
    EXPECT_EQ(ds_->get_access_read_count(full), 10u);
    
    // Decay with 0 protection → immediate decay
    ds_->decay_remote_access(0, 50);  // 50% decay factor
    EXPECT_EQ(ds_->get_access_read_count(full), 5u);
}

TEST_F(DataServiceTest, RemoteObjectMetaResetClearsAccess) {
    CMString full = db32("reset_test") + ":obj";
    ds_->update_remote_idx(full, 1, "host1", 1234);
    ds_->record_remote_access(full);
    EXPECT_EQ(ds_->get_access_read_count(full), 1u);

    ds_->reset();
    EXPECT_EQ(ds_->get_access_read_count(full), 0u);
}

// ─── Read API coverage (raw / wait / remote) ───

// try_read_local_raw returns (found, compressed_bytes) for a flushed object.
TEST_F(DataServiceTest, TryReadLocalRawReturnsCompressedData) {
    CMString db_path = test_dir_ + "/raw_read";
    Database db(db_path);
    write_raw(db, "raw/obj", "payload", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("raw/obj");
    auto [found, comp] = ds_->try_read_local_raw(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!comp || comp->empty());
}

// try_read_local_raw for unknown object returns false.
TEST_F(DataServiceTest, TryReadLocalRawReturnsFalseForUnknown) {
    auto [found, comp] = ds_->try_read_local_raw("unknown_obj");
    EXPECT_FALSE(found);
    EXPECT_TRUE(!comp || comp->empty());
}

// try_read_local_raw short-circuits via ObjectCache low tier: after a prior
// read_object_compressed populates the low tier, a subsequent try_read_local_raw
// must serve from cache (no disk IO). We prove this by deleting the on-disk
// .dat files after populating the cache — if it still returns data, it came
// from the low-tier cache, not disk.
TEST_F(DataServiceTest, TryReadLocalRawServesFromLowCache) {
    CMString db_path = test_dir_ + "/serve_cache";
    Database db(db_path);
    write_raw(db, "serve/obj", "payload", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("serve/obj");
    fly::ObjectCache::instance().clear();

    // Populate low tier via read_object_compressed.
    auto [comp, py_name] = db.read_object_compressed("serve/obj", false);
    ASSERT_FALSE(!comp || comp->empty());
    ASSERT_EQ(fly::ObjectCache::instance().low_size(), 1u);

    // Delete on-disk data files so a disk read would fail.
    for (auto& p : std::filesystem::directory_iterator(db_path)) {
        if (p.path().extension() == ".dat") {
            std::filesystem::remove(p.path());
        }
    }

    // try_read_local_raw must still succeed via the low-tier short-circuit.
    auto [found, raw] = ds_->try_read_local_raw(full);
    EXPECT_TRUE(found) << "should serve from cache even with disk files removed";
    EXPECT_EQ(raw, comp);

    fly::ObjectCache::instance().clear();
}

// try_read_local_raw populates the low tier after a disk read, so subsequent
// calls hit the cache.
TEST_F(DataServiceTest, TryReadLocalRawPopulatesLowCache) {
    CMString db_path = test_dir_ + "/populate_cache";
    Database db(db_path);
    write_raw(db, "pop/obj", "data", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("pop/obj");
    fly::ObjectCache::instance().clear();
    EXPECT_EQ(fly::ObjectCache::instance().low_size(), 0u);

    // First try_read_local_raw reads from disk and populates the low tier.
    auto [found, raw] = ds_->try_read_local_raw(full);
    ASSERT_TRUE(found);
    ASSERT_FALSE(!raw || raw->empty());
    EXPECT_EQ(fly::ObjectCache::instance().low_size(), 1u)
        << "try_read_local_raw should populate low tier after disk read";

    fly::ObjectCache::instance().clear();
}

// try_read_local_raw_or_wait returns immediately for a complete object (no wait).
TEST_F(DataServiceTest, TryReadLocalRawOrWaitImmediateForComplete) {
    CMString db_path = test_dir_ + "/raw_wait";
    Database db(db_path);
    write_raw(db, "rw/obj", "data", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("rw/obj");
    auto [found, comp, py_name] = ds_->try_read_local_raw_or_wait(full, 1000);
    EXPECT_TRUE(found);
    EXPECT_FALSE(!comp || comp->empty());
}

// try_read_local_raw_or_wait times out for an object that never completes.
TEST_F(DataServiceTest, TryReadLocalRawOrWaitTimesOut) {
    CMString db_path = db32("waitraw_db");
    CMString full = db_path + ":never_obj";
    // Register db so lookup doesn't early-return on unknown db.
    ds_->register_database(test_dir_ + "/waitraw", "", "writer_x");
    ds_->on_write_started(db_path, full);

    auto t0 = std::chrono::steady_clock::now();
    auto [found, comp, py_name] = ds_->try_read_local_raw_or_wait(full, 200);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0).count();
    EXPECT_FALSE(found);
    EXPECT_GE(elapsed, 150);
}

// try_read_local_or_wait (decoded) returns immediately for complete object.
TEST_F(DataServiceTest, TryReadLocalOrWaitImmediateForComplete) {
    CMString db_path = test_dir_ + "/or_wait";
    Database db(db_path);
    write_raw(db, "ow/obj", "hello", false);
    ds_->drain_write_back();

    CMString full = db.get_full_name("ow/obj");
    auto [found, result] = ds_->try_read_local_or_wait(full, 1000);
    EXPECT_TRUE(found);
}

// try_read_remote invokes the configured remote read handler. Under the new
// model the handler is a pure location query (returns refreshed signal, no
// data); without a direct handler the read cannot fetch, so it fails — but the
// handler must still have been invoked.
TEST_F(DataServiceTest, TryReadRemoteInvokesHandler) {
    CMString full = db32("remote_db") + ":rmt_obj";

    bool handler_called = false;
    ds_->set_remote_compressed_read_handler(
        [&full, &handler_called](const CMString& name) -> std::tuple<bool, bool> {
            EXPECT_EQ(name, full);
            handler_called = true;
            // Report "master knows of no location" to terminate cleanly.
            return {false, false};
        });

    auto [found, result] = ds_->try_read_remote(full);
    EXPECT_FALSE(found);
    EXPECT_TRUE(handler_called);
}

// is_write_in_progress reflects on_write_started / on_write_completed lifecycle.
TEST_F(DataServiceTest, IsWriteInProgressReflectsLifecycle) {
    CMString db_path = db32("wip_db");
    CMString full = db_path + ":wip_obj";
    ds_->register_database(test_dir_ + "/wip", "", "writer_w");
    EXPECT_FALSE(ds_->is_write_in_progress(full));

    ds_->on_write_started(db_path, full);
    EXPECT_TRUE(ds_->is_write_in_progress(full));
}

// TIER2 must try replicas in net-quality order: the better-scored host is
// contacted first. Two replicas registered as host_a then host_b; we score
// host_b higher, so the contact order must flip to b, a. Each replica returns
// OBJECT_NOT_FOUND so the full round is observed (no early success shortcut).
TEST_F(DataServiceTest, Tier2PrefersHigherScoredReplica) {
    auto& mon = fly::NetQualityMonitor::instance();
    mon.clear();
    CMString full = db32("tiersort") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);
    mon.update_rtt("host_b", 1.0);   // host_b: low RTT → high score
    mon.update_rtt("host_a", 200.0); // host_a: high RTT → low score

    std::vector<CMString> order;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t /*port*/, const CMString& /*name*/)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            order.push_back(host);
            return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
        });

    ds_->read_raw_compressed(full);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "host_b");  // higher score first
    EXPECT_EQ(order[1], "host_a");
    mon.clear();
}

// With equal (or no) scores, stable_sort must preserve registration order, so
// cold-start behavior is identical to before the feature.
TEST_F(DataServiceTest, Tier2KeepsRegistrationOrderWhenScoresEqual) {
    auto& mon = fly::NetQualityMonitor::instance();
    mon.clear();
    CMString full = db32("tierstable") + ":obj";
    ds_->update_remote_idx(full, 1, "host_a", 8000);
    ds_->update_remote_idx(full, 2, "host_b", 9000);

    std::vector<CMString> order;
    ds_->set_direct_compressed_read_handler(
        [&](const CMString& host, int32_t /*port*/, const CMString& /*name*/)
            -> std::tuple<bool, fly::CMSharedPtr<FlyBuffer>, CMString, CMString, fly::ReadError> {
            order.push_back(host);
            return {false, nullptr, {}, {}, fly::ReadError::OBJECT_NOT_FOUND};
        });

    ds_->read_raw_compressed(full);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "host_a");  // registration order preserved
    EXPECT_EQ(order[1], "host_b");
    mon.clear();
}

// ── DB Migration Redirect (resolve_migrated_path) ──

TEST_F(DataServiceTest, ResolveMigratedPath_NoMigration_ReturnsOriginal) {
    CMString path_a = test_dir_ + "/db_no_migrate";
    std::filesystem::create_directories(path_a);

    // 无 _MIGRATED_TO 文件 → 返回原 path
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_a);
}

TEST_F(DataServiceTest, ResolveMigratedPath_SingleHop_ReturnsTarget) {
    CMString path_a = test_dir_ + "/db_source";
    CMString path_b = test_dir_ + "/db_target";
    std::filesystem::create_directories(path_a);
    std::filesystem::create_directories(path_b);

    // 写 _MIGRATED_TO: A → B
    fly::DataService::write_migration_marker(path_a, path_b, path_b + "/data");

    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);
}

TEST_F(DataServiceTest, ResolveMigratedPath_ChainedHop_Flattened) {
    // 链式迁移 A → B → C，resolve(A) 应返回 C（展平）
    CMString path_a = test_dir_ + "/chain_a";
    CMString path_b = test_dir_ + "/chain_b";
    CMString path_c = test_dir_ + "/chain_c";
    for (const auto& p : {path_a, path_b, path_c}) {
        std::filesystem::create_directories(p);
    }

    fly::DataService::write_migration_marker(path_a, path_b, path_b + "/data");
    fly::DataService::write_migration_marker(path_b, path_c, path_c + "/data");

    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_c);
}

TEST_F(DataServiceTest, ResolveMigratedPath_CachedOnSecondCall) {
    CMString path_a = test_dir_ + "/db_cached_src";
    CMString path_b = test_dir_ + "/db_cached_tgt";
    std::filesystem::create_directories(path_a);
    std::filesystem::create_directories(path_b);

    fly::DataService::write_migration_marker(path_a, path_b, path_b + "/data");

    // 第一次：stat 文件解析
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);
    // 删除 _MIGRATED_TO 文件，第二次应仍返回缓存值（证明走了缓存）
    std::filesystem::remove(path_a + "/_MIGRATED_TO");
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);
}

TEST_F(DataServiceTest, SetMigratedPath_UpdatesCache) {
    CMString path_a = test_dir_ + "/db_set_src";
    CMString path_b = test_dir_ + "/db_set_tgt";
    std::filesystem::create_directories(path_a);
    std::filesystem::create_directories(path_b);

    // 主动设置缓存（merge 后 master 调用，不写文件）
    ds_->set_migrated_path(path_a, path_b);
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_b);

    // 清除缓存
    ds_->set_migrated_path(path_a, "");
    // 清除后 resolve 会 stat 文件（无 _MIGRATED_TO）→ 返回原 path
    EXPECT_EQ(ds_->resolve_migrated_path(path_a), path_a);
}

}
