#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <filesystem>
#include <istream>

namespace {

static CMString write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    return db.write_object_raw_ptr(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
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

TEST_F(DataServiceTest, UnflushedObjectNotReadable) {
    CMString db_id = db32("test_db");
    CMString full = db_id + ":pending/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    EXPECT_FALSE(ds_.has_local_object(full));

    auto [found, result] = ds_.try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, FlushMarksObjectsAsReadable) {
    CMString db_id = db32("flush_db");
    CMString full = db_id + ":flush/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;

    ds_.on_object_written(db_id, full, entry);
    EXPECT_FALSE(ds_.has_local_object(full));

    ds_.on_flush(db_id);
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

    db.write_object_raw_ptr("typed/ds_obj", "typed_payload", 13, "MyType");
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

TEST_F(DataServiceTest, FlushOnlyAffectsTargetDb) {
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

    ds_.on_object_written(db_a, full_a, ea);
    ds_.on_object_written(db_b, full_b, eb);

    ds_.on_flush(db_a);

    EXPECT_TRUE(ds_.has_local_object(full_a));
    EXPECT_FALSE(ds_.has_local_object(full_b));

    ds_.on_flush(db_b);
    EXPECT_TRUE(ds_.has_local_object(full_b));
}

}
