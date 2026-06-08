#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <filesystem>
#include <istream>

namespace {

static CMString write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    return db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
}

static CMString db32(const CMString& hint) {
    CMString r = hint;
    r.resize(32, '_');
    return r;
}

class DataServiceTest : public ::testing::Test {
protected:
    CMString test_dir_;
    fly::DataService& ds_ = fly::DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_ds_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DataServiceTest, TryReadLocalReturnsFalseForUnknownObject) {
    auto [found, result] = ds_.try_read_local("no_such_object");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, HasLocalObjectReturnsFalseWhenEmpty) {
    EXPECT_FALSE(ds_.has_local_object("anything"));
}

TEST_F(DataServiceTest, OnObjectWrittenAndFlushEnablesLocalRead) {
    CMString base_path = test_dir_ + "/local_read";
    Database db(base_path);

    write_raw(db, "local/obj", "hello", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("local/obj");
    EXPECT_TRUE(ds_.has_local_object(full));

    auto [found, result] = ds_.try_read_local(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "hello");
}

TEST_F(DataServiceTest, IncompleteObjectNotReadable) {
    CMString db_id = db32("test_db");
    CMString full = db_id + ":pending/obj";

    ds_.on_write_started(db_id, full);
    EXPECT_FALSE(ds_.has_local_object(full));

    auto [found, result] = ds_.try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, WriteCompletedMarksObjectReadable) {
    CMString db_id = db32("flush_db");
    CMString full = db_id + ":flush/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_write_started(db_id, full);
    EXPECT_FALSE(ds_.has_local_object(full));

    CMVector<IndexEntry> entries = {entry};
    ds_.on_write_completed(db_id, full, entries);
    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, UpdateRemoteIdxAndLookup) {
    CMString full = db32("remote") + ":obj";
    EXPECT_FALSE(ds_.has_remote_location(full));

    ds_.update_remote_idx(full, 42, "192.168.1.10", 9000);

    EXPECT_TRUE(ds_.has_remote_location(full));

    auto info = ds_.lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id, 42u);
    EXPECT_EQ(info.host, "192.168.1.10");
    EXPECT_EQ(info.port, 9000);
}

TEST_F(DataServiceTest, LookupRemoteIdxReturnsEmptyForUnknown) {
    auto info = ds_.lookup_remote_idx("unknown/obj");
    EXPECT_EQ(info.worker_id, 0u);
    EXPECT_TRUE(info.host.empty());
    EXPECT_EQ(info.port, 0);
}

TEST_F(DataServiceTest, RegisterWorkerAndGetAddress) {
    ds_.register_worker(1, "10.0.0.1", 8001);
    ds_.register_worker(2, "10.0.0.2", 8002);

    auto addr1 = ds_.get_worker_address(1);
    EXPECT_EQ(addr1.host, "10.0.0.1");
    EXPECT_EQ(addr1.port, 8001);

    auto addr2 = ds_.get_worker_address(2);
    EXPECT_EQ(addr2.host, "10.0.0.2");
    EXPECT_EQ(addr2.port, 8002);
}

TEST_F(DataServiceTest, GetWorkerAddressReturnsEmptyForUnknown) {
    auto addr = ds_.get_worker_address(9999);
    EXPECT_EQ(addr.worker_id, 0u);
    EXPECT_TRUE(addr.host.empty());
}

TEST_F(DataServiceTest, MultipleObjectsInSameDatabase) {
    CMString base_path = test_dir_ + "/multi_obj";
    Database db(base_path);

    write_raw(db, "multi/a", "data_a", false);
    write_raw(db, "multi/b", "data_b", false);
    write_raw(db, "multi/c", "data_c", false);
    fly::DataService::instance().drain_write_back();

    EXPECT_TRUE(ds_.has_local_object(db.get_obj_name("multi/a")));
    EXPECT_TRUE(ds_.has_local_object(db.get_obj_name("multi/b")));
    EXPECT_TRUE(ds_.has_local_object(db.get_obj_name("multi/c")));

    auto [fa, ra] = ds_.try_read_local(db.get_obj_name("multi/a"));
    EXPECT_TRUE(fa);
    CMString da(ra.data_buffer.begin(), ra.data_buffer.end());
    EXPECT_EQ(da, "data_a");

    auto [fb, rb] = ds_.try_read_local(db.get_obj_name("multi/b"));
    EXPECT_TRUE(fb);
    CMString db2(rb.data_buffer.begin(), rb.data_buffer.end());
    EXPECT_EQ(db2, "data_b");
}

TEST_F(DataServiceTest, TypedObjectReadableViaDataService) {
    CMString base_path = test_dir_ + "/typed_ds";
    Database db(base_path);

    db.write_pickle_bytes("typed/ds_obj", "typed_payload", 13, "MyType");
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("typed/ds_obj");
    auto [found, result] = ds_.try_read_local(full);
    EXPECT_TRUE(found);
    EXPECT_EQ(result.py_name, "MyType");
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "typed_payload");
}

TEST_F(DataServiceTest, RemoteIdxSupportsMultipleWorkers) {
    CMString full = db32("multi") + ":obj";
    ds_.update_remote_idx(full, 1, "host_a", 8000);
    ds_.update_remote_idx(full, 2, "host_b", 9000);

    // Both workers should be tracked
    auto workers = ds_.get_remote_workers(full);
    EXPECT_EQ(workers.size(), 2u);
    EXPECT_EQ(workers[0], 1u);
    EXPECT_EQ(workers[1], 2u);

    // Lookup returns first registered worker
    auto info = ds_.lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id, 1u);
    EXPECT_EQ(info.host, "host_a");
    EXPECT_EQ(info.port, 8000);
}

TEST_F(DataServiceTest, RegisterDatabaseAndLocalRead) {
    CMString db_id = db32("manual_db");
    CMString base_path = test_dir_ + "/reg_db";
    std::filesystem::create_directories(base_path);

    ds_.register_database(db_id, base_path, "");

    CMString full = db_id + ":manual/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    ds_.on_flush(db_id);

    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, RemoveLocalIndexMakesObjectUnreadable) {
    CMString base_path = test_dir_ + "/remove_local";
    Database db(base_path);

    write_raw(db, "remove/local", "data", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("remove/local");
    EXPECT_TRUE(ds_.has_local_object(full));

    ds_.remove_local_index(full);

    EXPECT_FALSE(ds_.has_local_object(full));

    auto [found, result] = ds_.try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, RemoveLocalIndexOnlyAffectsTarget) {
    CMString base_path = test_dir_ + "/remove_one_local";
    Database db(base_path);

    write_raw(db, "keep/me", "keep_data", false);
    write_raw(db, "remove/me", "remove_data", false);
    fly::DataService::instance().drain_write_back();

    CMString keep_full = db.get_obj_name("keep/me");
    CMString remove_full = db.get_obj_name("remove/me");

    ds_.remove_local_index(remove_full);

    EXPECT_TRUE(ds_.has_local_object(keep_full));
    EXPECT_FALSE(ds_.has_local_object(remove_full));
}

TEST_F(DataServiceTest, RemoveRemoteIndexClearsLocation) {
    CMString full = db32("remote") + ":remove_test";
    ds_.update_remote_idx(full, 1, "host_a", 8000);
    EXPECT_TRUE(ds_.has_remote_location(full));

    ds_.remove_remote_index(full);

    EXPECT_FALSE(ds_.has_remote_location(full));

    auto info = ds_.lookup_remote_idx(full);
    EXPECT_EQ(info.worker_id, 0u);
    EXPECT_TRUE(info.host.empty());
}

TEST_F(DataServiceTest, RemoveRemoteIndexOnlyAffectsTarget) {
    CMString keep_full = db32("remote") + ":keep";
    CMString remove_full = db32("remote") + ":remove";
    ds_.update_remote_idx(keep_full, 1, "host_a", 8000);
    ds_.update_remote_idx(remove_full, 2, "host_b", 9000);

    ds_.remove_remote_index(remove_full);

    EXPECT_TRUE(ds_.has_remote_location(keep_full));
    EXPECT_FALSE(ds_.has_remote_location(remove_full));
}

TEST_F(DataServiceTest, RestoreEntriesMakesObjectsReadable) {
    CMString db_id = db32("restore_db");
    CMString base_path = test_dir_ + "/restore_test";
    std::filesystem::create_directories(base_path);
    ds_.register_database(db_id, base_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name = db_id + ":obj_a";
    e1.file_name = "test.dat";
    e1.offset = 0;
    e1.size = 5;
    e1.is_large = false;
    e1.block_count = 0;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name = db_id + ":obj_b";
    e2.file_name = "test.dat";
    e2.offset = 10;
    e2.size = 7;
    e2.is_large = false;
    e2.block_count = 0;
    entries.push_back(e2);

    ds_.restore_entries(db_id, entries);

    EXPECT_TRUE(ds_.has_local_object(db_id + ":obj_a"));
    EXPECT_TRUE(ds_.has_local_object(db_id + ":obj_b"));
}

TEST_F(DataServiceTest, RestoreEntriesMultipleEntriesPerObject) {
    CMString db_id = db32("multi_db");
    CMString base_path = test_dir_ + "/restore_multi";
    std::filesystem::create_directories(base_path);
    ds_.register_database(db_id, base_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name = db_id + ":large_obj";
    e1.file_name = "data_0.dat";
    e1.offset = 0;
    e1.size = 100;
    e1.is_large = true;
    e1.block_count = 1;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name = db_id + ":large_obj";
    e2.file_name = "data_0.dat";
    e2.offset = 100;
    e2.size = 200;
    e2.is_large = true;
    e2.block_count = 1;
    entries.push_back(e2);

    ds_.restore_entries(db_id, entries);

    EXPECT_TRUE(ds_.has_local_object(db_id + ":large_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesAppendsToExisting) {
    CMString db_id = db32("append_db");
    CMString base_path = test_dir_ + "/restore_append";
    std::filesystem::create_directories(base_path);
    ds_.register_database(db_id, base_path, "");

    IndexEntry e1;
    e1.object_name = db_id + ":obj";
    e1.file_name = "test.dat";
    e1.offset = 0;
    e1.size = 5;
    e1.is_large = false;
    e1.block_count = 0;
    ds_.on_object_written(db_id, db_id + ":obj", e1);
    ds_.on_flush(db_id);
    EXPECT_TRUE(ds_.has_local_object(db_id + ":obj"));

    CMVector<IndexEntry> restore_entries_vec;
    IndexEntry e2;
    e2.object_name = db_id + ":new_obj";
    e2.file_name = "test.dat";
    e2.offset = 100;
    e2.size = 10;
    e2.is_large = false;
    e2.block_count = 0;
    restore_entries_vec.push_back(e2);

    ds_.restore_entries(db_id, restore_entries_vec);

    EXPECT_TRUE(ds_.has_local_object(db_id + ":obj"));
    EXPECT_TRUE(ds_.has_local_object(db_id + ":new_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesEmptyVectorIsNoop) {
    CMVector<IndexEntry> empty;
    ds_.restore_entries(db32("empty_db"), empty);
}

TEST_F(DataServiceTest, RestoreEntriesFromLocalIndexFile) {
    CMString base_path = test_dir_ + "/restore_from_idx";
    Database db(base_path);

    write_raw(db, "idx/obj1", "data1", false);
    write_raw(db, "idx/obj2", "data2", false);
    fly::DataService::instance().drain_write_back();

    CMString idx_path = base_path + "/" + db.get_writer_id() + ".idx";

    LocalIndex source_idx(idx_path);
    source_idx.load();
    auto all_entries = source_idx.get_all_entries();
    ASSERT_FALSE(all_entries.empty());

    CMString original_db_id = db.get_db_id();
    CMString restore_base = test_dir_ + "/restored_db";
    std::filesystem::create_directories(restore_base);
    ds_.register_database(original_db_id, restore_base, "");

    ds_.restore_entries(original_db_id, all_entries);

    for (const auto& e : all_entries) {
        EXPECT_TRUE(ds_.has_local_object(e.object_name));
    }
}

TEST_F(DataServiceTest, ShortNameWithColonsHandledCorrectly) {
    CMString db_id = db32("colon_test");
    CMString full = db_id + ":obj/with:colons:inside";

    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    ds_.on_flush(db_id);

    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, DbIdExactly32Chars) {
    CMString db_id(32, 'a');
    CMString full = db_id + ":my_obj";

    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    ds_.on_flush(db_id);

    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, ShortFullNameTreatedAsNoDbId) {
    CMString short_name = "short_obj_name";

    IndexEntry entry;
    entry.object_name = short_name;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written("", short_name, entry);
    ds_.on_flush("");

    EXPECT_TRUE(ds_.has_local_object(short_name));
}

TEST_F(DataServiceTest, RemoveIndexByShortName) {
    CMString db_id = db32("remove_short");
    CMString full = db_id + ":remove_target";

    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    ds_.on_flush(db_id);
    EXPECT_TRUE(ds_.has_local_object(full));

    ds_.remove_local_index(full);
    EXPECT_FALSE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, WriteCompletedOnlyAffectsTargetDb) {
    CMString db_a = db32("flush_db_a");
    CMString db_b = db32("flush_db_b");
    CMString full_a = db_a + ":obj_a";
    CMString full_b = db_b + ":obj_b";

    IndexEntry ea;
    ea.object_name = full_a;
    ea.file_name = "test.dat";
    ea.offset = 0;
    ea.size = 5;
    ea.is_large = false;
    ea.block_count = 0;

    IndexEntry eb;
    eb.object_name = full_b;
    eb.file_name = "test.dat";
    eb.offset = 0;
    eb.size = 5;
    eb.is_large = false;
    eb.block_count = 0;

    ds_.on_write_started(db_a, full_a);
    ds_.on_write_started(db_b, full_b);

    CMVector<IndexEntry> entries_a = {ea};
    ds_.on_write_completed(db_a, full_a, entries_a);
    EXPECT_TRUE(ds_.has_local_object(full_a));
    EXPECT_FALSE(ds_.has_local_object(full_b));

    CMVector<IndexEntry> entries_b = {eb};
    ds_.on_write_completed(db_b, full_b, entries_b);
    EXPECT_TRUE(ds_.has_local_object(full_b));
}

TEST_F(DataServiceTest, MarkTempEntryAndGet) {
    CMString name = "temp/obj";
    CMString data = "temp_data_payload";

    ds_.mark_temp_entry(name, data);
    EXPECT_TRUE(ds_.is_temp_entry(name));

    auto [found, result] = ds_.get_temp_data(name);
    EXPECT_TRUE(found);
    EXPECT_EQ(result, data);
}

TEST_F(DataServiceTest, UnmarkTempEntry) {
    CMString name = "temp/remove";
    CMString data = "to_remove";

    ds_.mark_temp_entry(name, data);
    EXPECT_TRUE(ds_.is_temp_entry(name));

    ds_.unmark_temp_entry(name);
    EXPECT_FALSE(ds_.is_temp_entry(name));

    auto [found, result] = ds_.get_temp_data(name);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, IsTempEntryReturnsFalseForMissing) {
    EXPECT_FALSE(ds_.is_temp_entry("never/temp"));
}

TEST_F(DataServiceTest, GetTempDataReturnsFalseForMissing) {
    auto [found, result] = ds_.get_temp_data("no/temp");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, MarkTempEntrySameKey) {
    CMString name = "temp/same";
    ds_.mark_temp_entry(name, "data1");

    auto [found, result] = ds_.get_temp_data(name);
    EXPECT_TRUE(found);
    EXPECT_EQ(result, "data1");
}

TEST_F(DataServiceTest, HasDatabaseReturnsTrue) {
    CMString db_id = db32("has_db");
    CMString base_path = test_dir_ + "/has_db";
    std::filesystem::create_directories(base_path);
    ds_.register_database(db_id, base_path, "");
    EXPECT_TRUE(ds_.has_database(db_id));
}

TEST_F(DataServiceTest, HasDatabaseReturnsFalseForUnknown) {
    EXPECT_FALSE(ds_.has_database(db32("unknown_db")));
}

TEST_F(DataServiceTest, UnregisterDatabaseRemovesIt) {
    CMString db_id = db32("unreg_db");
    CMString base_path = test_dir_ + "/unreg_db";
    std::filesystem::create_directories(base_path);
    ds_.register_database(db_id, base_path, "");
    EXPECT_TRUE(ds_.has_database(db_id));

    ds_.unregister_database(db_id);
    EXPECT_FALSE(ds_.has_database(db_id));
}

TEST_F(DataServiceTest, RemoveRemoteLocationByWorkerId) {
    CMString full = db32("rr_worker") + ":obj";
    ds_.update_remote_idx(full, 1, "host_a", 8000);
    ds_.update_remote_idx(full, 2, "host_b", 9000);

    auto workers = ds_.get_remote_workers(full);
    EXPECT_EQ(workers.size(), 2u);

    ds_.remove_remote_location(full, 1);

    workers = ds_.get_remote_workers(full);
    EXPECT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 2u);
}

TEST_F(DataServiceTest, RemoveRemoteLocationByWorkerIdCleansUpWhenEmpty) {
    CMString full = db32("rr_cleanup") + ":obj";
    ds_.update_remote_idx(full, 1, "host_a", 8000);

    ds_.remove_remote_location(full, 1);

    EXPECT_FALSE(ds_.has_remote_location(full));
    EXPECT_TRUE(ds_.get_remote_workers(full).empty());
}

TEST_F(DataServiceTest, OnObjectWrittenSetsComplete) {
    CMString db_id = db32("flush_obj_db");
    CMString full = db_id + ":flush/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteStartedCreatesEntry) {
    CMString db_id = db32("start_db");
    CMString full = db_id + ":started/obj";

    ds_.on_write_started(db_id, full);
    EXPECT_FALSE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteFailedRemovesEntry) {
    CMString db_id = db32("fail_db");
    CMString full = db_id + ":failed/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    ds_.on_flush(db_id);
    EXPECT_TRUE(ds_.has_local_object(full));

    ds_.on_write_failed(db_id, full, "error msg");
    EXPECT_FALSE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, AddRemoteLocation) {
    CMString full = db32("add_remote") + ":obj";
    ds_.register_worker(1, "host_a", 8000);
    ds_.add_remote_location(full, 1);

    EXPECT_TRUE(ds_.has_remote_location(full));
    auto workers = ds_.get_remote_workers(full);
    EXPECT_EQ(workers.size(), 1u);
    EXPECT_EQ(workers[0], 1u);
}

TEST_F(DataServiceTest, GetRemoteWorkersEmptyForMissing) {
    auto workers = ds_.get_remote_workers("no/such/obj");
    EXPECT_TRUE(workers.empty());
}

TEST_F(DataServiceTest, HasRemoteLocationFalseForMissing) {
    EXPECT_FALSE(ds_.has_remote_location("missing/remote"));
}

TEST_F(DataServiceTest, WriteBackQueueStartStop) {
    ds_.stop_write_back();
    EXPECT_FALSE(ds_.is_write_back_running());

    ds_.start_write_back();
    EXPECT_TRUE(ds_.is_write_back_running());

    ds_.stop_write_back();
    EXPECT_FALSE(ds_.is_write_back_running());
}

TEST_F(DataServiceTest, FindLocalEntriesReturnsNoneForMissing) {
    auto entries = ds_.find_local_entries("no/local/entries");
    EXPECT_FALSE(entries.has_value());
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsFalseForMissingDb) {
    auto [found, result] = ds_.try_read_local_or_wait("no_such_object", 100);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsFalseForMissingEntry) {
    CMString db_id = db32("wait_missing");
    auto [found, result] = ds_.try_read_local_or_wait(db_id + ":no_entry", 100);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsImmediatelyWhenComplete) {
    CMString base_path = test_dir_ + "/wait_read";
    Database db(base_path);

    write_raw(db, "wait/obj", "wait_data", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("wait/obj");
    auto [found, result] = ds_.try_read_local_or_wait(full, 100);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "wait_data");
}

TEST_F(DataServiceTest, TryReadLocalOrWaitTimeoutOnIncomplete) {
    CMString db_id = db32("wait_timeout");
    CMString full = db_id + ":pending_obj";

    ds_.on_write_started(db_id, full);

    auto [found, result] = ds_.try_read_local_or_wait(full, 50);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalOrWaitReturnsFalseOnFailed) {
    CMString db_id = db32("wait_fail");
    CMString full = db_id + ":fail_obj";

    ds_.on_write_started(db_id, full);
    ds_.on_write_failed(db_id, full, "test error");

    auto [found, result] = ds_.try_read_local_or_wait(full, 100);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalRawReturnsData) {
    CMString base_path = test_dir_ + "/raw_read";
    Database db(base_path);

    write_raw(db, "raw/obj", "raw_data", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("raw/obj");
    auto [found, raw] = ds_.try_read_local_raw(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(raw.empty());
}

TEST_F(DataServiceTest, TryReadLocalRawReturnsFalseForMissing) {
    auto [found, raw] = ds_.try_read_local_raw("missing/obj");
    EXPECT_FALSE(found);
    EXPECT_TRUE(raw.empty());
}

TEST_F(DataServiceTest, TryReadLocalRawOrWaitReturnsData) {
    CMString base_path = test_dir_ + "/raw_wait";
    Database db(base_path);

    write_raw(db, "rawwait/obj", "rawwait_data", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("rawwait/obj");
    auto [found, raw, py_name] = ds_.try_read_local_raw_or_wait(full, 100);
    EXPECT_TRUE(found);
    EXPECT_FALSE(raw.empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DataServiceTest, TryReadLocalRawOrWaitReturnsFalseForMissing) {
    auto [found, raw, py_name] = ds_.try_read_local_raw_or_wait("missing_raw/obj", 50);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadLocalRawOrWaitTimeoutOnIncomplete) {
    CMString db_id = db32("raw_timeout");
    CMString full = db_id + ":incomplete_raw";
    ds_.on_write_started(db_id, full);

    auto [found, raw, py_name] = ds_.try_read_local_raw_or_wait(full, 50);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadRemoteReturnsLocalIfAvailable) {
    CMString base_path = test_dir_ + "/remote_local";
    Database db(base_path);

    write_raw(db, "remote/local_obj", "local_data", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("remote/local_obj");
    auto [found, result] = ds_.try_read_remote(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "local_data");
}

TEST_F(DataServiceTest, TryReadRemoteReturnsFalseForMissing) {
    auto [found, result] = ds_.try_read_remote("no_such_remote_obj");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, TryReadRemoteSetsCanStillProduceFlag) {
    auto [found, result] = ds_.try_read_remote("missing_remote_data");
    EXPECT_FALSE(found);
    EXPECT_FALSE(result.can_still_produce);
}

TEST_F(DataServiceTest, ReadRawCompressedReturnsLocalRaw) {
    CMString base_path = test_dir_ + "/raw_comp";
    Database db(base_path);

    write_raw(db, "comp/obj", "comp_data", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("comp/obj");
    auto [found, raw, py_name, hash, can_still] = ds_.read_raw_compressed(full);
    EXPECT_TRUE(found);
    EXPECT_FALSE(raw.empty());
    EXPECT_EQ(py_name, "bytes");
}

TEST_F(DataServiceTest, ReadRawCompressedReturnsFalseForMissing) {
    auto [found, raw, py_name, hash, can_still] = ds_.read_raw_compressed("missing_comp");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, RegisterDatabaseDuplicateUpdatesExisting) {
    CMString db_id = db32("dup_db");
    CMString base1 = test_dir_ + "/dup_db1";
    CMString base2 = test_dir_ + "/dup_db2";
    std::filesystem::create_directories(base1);
    std::filesystem::create_directories(base2);

    ds_.register_database(db_id, base1, "");
    EXPECT_TRUE(ds_.has_database(db_id));

    ds_.register_database(db_id, base2, "");
    EXPECT_TRUE(ds_.has_database(db_id));
}

TEST_F(DataServiceTest, RegisterDatabaseDuplicateBasePathRejected) {
    CMString db1 = db32("first_db");
    CMString db2 = db32("second_db");
    CMString base = test_dir_ + "/shared_base";

    std::filesystem::create_directories(base);
    ds_.register_database(db1, base, "");
    ds_.register_database(db2, base, "");

    EXPECT_TRUE(ds_.has_database(db1));
    EXPECT_FALSE(ds_.has_database(db2));
}

TEST_F(DataServiceTest, RemoveRemoteLocationByFullObject) {
    CMString full = db32("rr_full") + ":obj";
    ds_.update_remote_idx(full, 1, "host_a", 8000);
    EXPECT_TRUE(ds_.has_remote_location(full));

    ds_.remove_remote_location(full);
    EXPECT_FALSE(ds_.has_remote_location(full));
}

TEST_F(DataServiceTest, RestoreEntriesWithShortObjectNames) {
    CMString db_id = db32("short_db");
    CMString base_path = test_dir_ + "/short_restore";
    std::filesystem::create_directories(base_path);
    ds_.register_database(db_id, base_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e;
    e.object_name = db_id + ":simple_name";
    e.file_name = "test.dat";
    e.offset = 0;
    e.size = 10;
    e.is_large = false;
    e.block_count = 0;
    entries.push_back(e);

    ds_.restore_entries(db_id, entries);
    EXPECT_TRUE(ds_.has_local_object(db_id + ":simple_name"));
}

TEST_F(DataServiceTest, OnWriteStartedAndCompletedCycle) {
    CMString db_id = db32("cycle_db");
    CMString full = db_id + ":cycle/obj";

    ds_.on_write_started(db_id, full);
    EXPECT_FALSE(ds_.has_local_object(full));

    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;

    CMVector<IndexEntry> entries = {entry};
    ds_.on_write_completed(db_id, full, entries);
    ds_.on_object_flushed(full);

    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, OnWriteCompletedForMissingDbIsNoop) {
    CMString db_id = db32("missing_db_wc");
    CMString full = db_id + ":missing/obj";
    CMVector<IndexEntry> entries;
    EXPECT_NO_THROW(ds_.on_write_completed(db_id, full, entries));
}

TEST_F(DataServiceTest, OnWriteFailedForMissingDbIsNoop) {
    CMString db_id = db32("missing_db_wf");
    CMString full = db_id + ":missing/obj";
    EXPECT_NO_THROW(ds_.on_write_failed(db_id, full, "error"));
}

TEST_F(DataServiceTest, OnFlushForMissingDbIsNoop) {
    EXPECT_NO_THROW(ds_.on_flush(db32("nonexistent_flush")));
}

TEST_F(DataServiceTest, FindLocalEntriesReturnsData) {
    CMString db_id = db32("find_db");
    CMString full = db_id + ":find/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    ds_.on_flush(db_id);

    auto found = ds_.find_local_entries(full);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->size(), 1u);
    EXPECT_EQ((*found)[0].object_name, full);
}

TEST_F(DataServiceTest, SetRemoteCompressedReadHandler) {
    bool called = false;
    ds_.set_remote_compressed_read_handler([&called](const CMString& name) {
        called = true;
        return std::make_tuple(false, CMString{}, CMString{}, false);
    });
    EXPECT_NO_THROW(ds_.set_remote_compressed_read_handler(nullptr));
}

TEST_F(DataServiceTest, SetDirectCompressedReadHandler) {
    ds_.set_direct_compressed_read_handler(
        [](const CMString& host, int32_t port, const CMString& name) {
            return std::make_tuple(false, CMString{}, CMString{}, CMString{});
        });
    EXPECT_NO_THROW(ds_.set_direct_compressed_read_handler(nullptr));
}

TEST_F(DataServiceTest, TransferServerStartStop) {
    EXPECT_FALSE(ds_.is_transfer_server_running());

    ds_.start_transfer_server(1, [](const fly::TransferResult&) {});
    EXPECT_TRUE(ds_.is_transfer_server_running());

    ds_.stop_transfer_server();
    EXPECT_FALSE(ds_.is_transfer_server_running());
}

TEST_F(DataServiceTest, EnqueueWriteBackAutoStarts) {
    ds_.stop_write_back();
    EXPECT_FALSE(ds_.is_write_back_running());

    fly::WriteRequest req;
    req.execute = []() {};
    req.on_complete = []() {};
    ds_.enqueue_write_back(std::move(req));

    EXPECT_TRUE(ds_.is_write_back_running());

    ds_.drain_write_back();
    ds_.stop_write_back();
}

TEST_F(DataServiceTest, ResetClearsAllState) {
    CMString db_id = db32("reset_db");
    ds_.register_database(db_id, test_dir_, "");
    ds_.register_worker(1, "host", 8000);

    ds_.reset();

    EXPECT_FALSE(ds_.has_database(db_id));
}

// ============================================================
// Auto-Backup Access Tracking (inline in remote_idx_)
// ============================================================

TEST_F(DataServiceTest, RemoteObjectMetaTracksReadCount) {
    CMString full = db32("meta_test") + ":obj";
    ds_.update_remote_idx(full, 1, "host1", 1234);
    
    // Access tracking is now on remote_idx_ directly
    ds_.record_remote_access(full);
    ds_.record_remote_access(full);
    
    EXPECT_EQ(ds_.get_access_read_count(full), 2u);
}

TEST_F(DataServiceTest, RemoteObjectMetaEvaluateAutoBackup) {
    CMString full = db32("eval_test") + ":obj";
    ds_.update_remote_idx(full, 1, "host1", 1234);
    
    for (int i = 0; i < 5; i++) {
        ds_.record_remote_access(full);
    }
    
    // 1 worker, threshold=3, target=2 → should_backup
    auto decision = ds_.evaluate_auto_backup(full, 3, 2);
    EXPECT_TRUE(decision.should_backup);
    EXPECT_EQ(decision.current_replicas, 1u);
    EXPECT_EQ(decision.target_replicas, 2u);
}

TEST_F(DataServiceTest, RemoteObjectMetaEvaluateSatisfied) {
    CMString full = db32("sat_test") + ":obj";
    ds_.update_remote_idx(full, 1, "host1", 1234);
    ds_.update_remote_idx(full, 2, "host2", 1235);
    
    for (int i = 0; i < 5; i++) {
        ds_.record_remote_access(full);
    }
    
    // 2 workers already, target=2 → NOT should_backup
    auto decision = ds_.evaluate_auto_backup(full, 3, 2);
    EXPECT_FALSE(decision.should_backup);
    EXPECT_EQ(decision.current_replicas, 2u);
}

TEST_F(DataServiceTest, RemoteObjectMetaDecay) {
    CMString full = db32("decay_test") + ":obj";
    ds_.update_remote_idx(full, 1, "host1", 1234);
    
    for (int i = 0; i < 10; i++) {
        ds_.record_remote_access(full);
    }
    EXPECT_EQ(ds_.get_access_read_count(full), 10u);
    
    // Decay with 0 protection → immediate decay
    ds_.decay_remote_access(0, 50);  // 50% decay factor
    EXPECT_EQ(ds_.get_access_read_count(full), 5u);
}

TEST_F(DataServiceTest, RemoteObjectMetaResetClearsAccess) {
    CMString full = db32("reset_test") + ":obj";
    ds_.update_remote_idx(full, 1, "host1", 1234);
    ds_.record_remote_access(full);
    EXPECT_EQ(ds_.get_access_read_count(full), 1u);
    
    ds_.reset();
    EXPECT_EQ(ds_.get_access_read_count(full), 0u);
}

}
