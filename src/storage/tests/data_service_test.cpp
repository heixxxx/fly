#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/local_index.h>
#include <filesystem>

namespace {

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

    db.write_object("local/obj", "hello", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("local/obj");
    EXPECT_TRUE(ds_.has_local_object(full));

    auto [found, result] = ds_.try_read_local(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "hello");
}

TEST_F(DataServiceTest, UnflushedObjectNotReadable) {
    CMString full = "test_db:pending/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;
    entry.compression_type = 0;

    ds_.on_object_written("test_db", full, entry);
    EXPECT_FALSE(ds_.has_local_object(full));

    auto [found, result] = ds_.try_read_local(full);
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, FlushMarksObjectsAsReadable) {
    CMString full = "flush_db:flush/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;
    entry.compression_type = 0;

    ds_.on_object_written("flush_db", full, entry);
    EXPECT_FALSE(ds_.has_local_object(full));

    ds_.on_flush("flush_db");
    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, UpdateRemoteIdxAndLookup) {
    EXPECT_FALSE(ds_.has_remote_location("remote/obj"));

    ds_.update_remote_idx("remote/obj", 42, "192.168.1.10", 9000);

    EXPECT_TRUE(ds_.has_remote_location("remote/obj"));

    auto info = ds_.lookup_remote_idx("remote/obj");
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

    db.write_object("multi/a", "data_a", false);
    db.write_object("multi/b", "data_b", false);
    db.write_object("multi/c", "data_c", false);
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

    db.write_object_typed("typed/ds_obj", "typed_payload", "MyType");
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("typed/ds_obj");
    auto [found, result] = ds_.try_read_local(full);
    EXPECT_TRUE(found);
    EXPECT_EQ(result.py_name, "MyType");
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "typed_payload");
}

TEST_F(DataServiceTest, RemoteIdxSupportsMultipleWorkers) {
    ds_.update_remote_idx("multi/obj", 1, "host_a", 8000);
    ds_.update_remote_idx("multi/obj", 2, "host_b", 9000);

    // Both workers should be tracked
    auto workers = ds_.get_remote_workers("multi/obj");
    EXPECT_EQ(workers.size(), 2u);
    EXPECT_EQ(workers[0], 1u);
    EXPECT_EQ(workers[1], 2u);

    // Lookup returns first registered worker
    auto info = ds_.lookup_remote_idx("multi/obj");
    EXPECT_EQ(info.worker_id, 1u);
    EXPECT_EQ(info.host, "host_a");
    EXPECT_EQ(info.port, 8000);
}

TEST_F(DataServiceTest, RegisterDatabaseAndLocalRead) {
    CMString base_path = test_dir_ + "/reg_db";
    std::filesystem::create_directories(base_path);

    ds_.register_database("manual_db", base_path, "");

    CMString full = "manual_db:manual/obj";
    IndexEntry entry;
    entry.object_name = full;
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;
    entry.compression_type = 0;

    ds_.on_object_written("manual_db", full, entry);
    ds_.on_flush("manual_db");

    EXPECT_TRUE(ds_.has_local_object(full));
}

TEST_F(DataServiceTest, RemoveLocalIndexMakesObjectUnreadable) {
    CMString base_path = test_dir_ + "/remove_local";
    Database db(base_path);

    db.write_object("remove/local", "data", false);
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

    db.write_object("keep/me", "keep_data", false);
    db.write_object("remove/me", "remove_data", false);
    fly::DataService::instance().drain_write_back();

    CMString keep_full = db.get_obj_name("keep/me");
    CMString remove_full = db.get_obj_name("remove/me");

    ds_.remove_local_index(remove_full);

    EXPECT_TRUE(ds_.has_local_object(keep_full));
    EXPECT_FALSE(ds_.has_local_object(remove_full));
}

TEST_F(DataServiceTest, RemoveRemoteIndexClearsLocation) {
    ds_.update_remote_idx("remote/remove_test", 1, "host_a", 8000);
    EXPECT_TRUE(ds_.has_remote_location("remote/remove_test"));

    ds_.remove_remote_index("remote/remove_test");

    EXPECT_FALSE(ds_.has_remote_location("remote/remove_test"));

    auto info = ds_.lookup_remote_idx("remote/remove_test");
    EXPECT_EQ(info.worker_id, 0u);
    EXPECT_TRUE(info.host.empty());
}

TEST_F(DataServiceTest, RemoveRemoteIndexOnlyAffectsTarget) {
    ds_.update_remote_idx("remote/keep", 1, "host_a", 8000);
    ds_.update_remote_idx("remote/remove", 2, "host_b", 9000);

    ds_.remove_remote_index("remote/remove");

    EXPECT_TRUE(ds_.has_remote_location("remote/keep"));
    EXPECT_FALSE(ds_.has_remote_location("remote/remove"));
}

TEST_F(DataServiceTest, RestoreEntriesMakesObjectsReadable) {
    CMString base_path = test_dir_ + "/restore_test";
    std::filesystem::create_directories(base_path);
    ds_.register_database("restore_db", base_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name = "restore_db:obj_a";
    e1.file_name = "test.dat";
    e1.offset = 0;
    e1.size = 5;
    e1.is_large = false;
    e1.block_count = 0;
    e1.compression_type = 0;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name = "restore_db:obj_b";
    e2.file_name = "test.dat";
    e2.offset = 10;
    e2.size = 7;
    e2.is_large = false;
    e2.block_count = 0;
    e2.compression_type = 0;
    entries.push_back(e2);

    ds_.restore_entries("restore_db", entries);

    EXPECT_TRUE(ds_.has_local_object("restore_db:obj_a"));
    EXPECT_TRUE(ds_.has_local_object("restore_db:obj_b"));
}

TEST_F(DataServiceTest, RestoreEntriesMultipleEntriesPerObject) {
    CMString base_path = test_dir_ + "/restore_multi";
    std::filesystem::create_directories(base_path);
    ds_.register_database("multi_db", base_path, "");

    CMVector<IndexEntry> entries;
    IndexEntry e1;
    e1.object_name = "multi_db:large_obj";
    e1.file_name = "data_0.dat";
    e1.offset = 0;
    e1.size = 100;
    e1.is_large = true;
    e1.block_count = 1;
    e1.compression_type = 0;
    entries.push_back(e1);

    IndexEntry e2;
    e2.object_name = "multi_db:large_obj";
    e2.file_name = "data_0.dat";
    e2.offset = 100;
    e2.size = 200;
    e2.is_large = true;
    e2.block_count = 1;
    e2.compression_type = 0;
    entries.push_back(e2);

    ds_.restore_entries("multi_db", entries);

    EXPECT_TRUE(ds_.has_local_object("multi_db:large_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesAppendsToExisting) {
    CMString base_path = test_dir_ + "/restore_append";
    std::filesystem::create_directories(base_path);
    ds_.register_database("append_db", base_path, "");

    IndexEntry e1;
    e1.object_name = "append_db:obj";
    e1.file_name = "test.dat";
    e1.offset = 0;
    e1.size = 5;
    e1.is_large = false;
    e1.block_count = 0;
    e1.compression_type = 0;
    ds_.on_object_written("append_db", "append_db:obj", e1);
    ds_.on_flush("append_db");
    EXPECT_TRUE(ds_.has_local_object("append_db:obj"));

    CMVector<IndexEntry> restore_entries_vec;
    IndexEntry e2;
    e2.object_name = "append_db:new_obj";
    e2.file_name = "test.dat";
    e2.offset = 100;
    e2.size = 10;
    e2.is_large = false;
    e2.block_count = 0;
    e2.compression_type = 0;
    restore_entries_vec.push_back(e2);

    ds_.restore_entries("append_db", restore_entries_vec);

    EXPECT_TRUE(ds_.has_local_object("append_db:obj"));
    EXPECT_TRUE(ds_.has_local_object("append_db:new_obj"));
}

TEST_F(DataServiceTest, RestoreEntriesEmptyVectorIsNoop) {
    CMVector<IndexEntry> empty;
    ds_.restore_entries("empty_db", empty);
}

TEST_F(DataServiceTest, RestoreEntriesFromLocalIndexFile) {
    CMString base_path = test_dir_ + "/restore_from_idx";
    Database db(base_path);

    db.write_object("idx/obj1", "data1", false);
    db.write_object("idx/obj2", "data2", false);
    fly::DataService::instance().drain_write_back();

    CMString idx_path = base_path + "/" + db.get_writer_id() + ".idx";

    LocalIndex source_idx(idx_path);
    source_idx.load();
    auto all_entries = source_idx.get_all_entries();
    ASSERT_FALSE(all_entries.empty());

    CMString restore_db_id = "restored_from_idx";
    CMString restore_base = test_dir_ + "/restored_db";
    std::filesystem::create_directories(restore_base);
    ds_.register_database(restore_db_id, restore_base, "");

    ds_.restore_entries(restore_db_id, all_entries);

    for (const auto& e : all_entries) {
        EXPECT_TRUE(ds_.has_local_object(e.object_name));
    }
}

}
