#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
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

    EXPECT_TRUE(ds_.has_local_object("local/obj"));

    auto [found, result] = ds_.try_read_local("local/obj");
    EXPECT_TRUE(found);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "hello");
}

TEST_F(DataServiceTest, UnflushedObjectNotReadable) {
    IndexEntry entry;
    entry.object_name = "pending/obj";
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;
    entry.compression_type = 0;

    ds_.on_object_written("test_db", "pending/obj", entry);
    EXPECT_FALSE(ds_.has_local_object("pending/obj"));

    auto [found, result] = ds_.try_read_local("pending/obj");
    EXPECT_FALSE(found);
}

TEST_F(DataServiceTest, FlushMarksObjectsAsReadable) {
    IndexEntry entry;
    entry.object_name = "flush/obj";
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 10;
    entry.is_large = false;
    entry.block_count = 0;
    entry.compression_type = 0;

    ds_.on_object_written("flush_db", "flush/obj", entry);
    EXPECT_FALSE(ds_.has_local_object("flush/obj"));

    ds_.on_flush("flush_db");
    EXPECT_TRUE(ds_.has_local_object("flush/obj"));
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

    EXPECT_TRUE(ds_.has_local_object("multi/a"));
    EXPECT_TRUE(ds_.has_local_object("multi/b"));
    EXPECT_TRUE(ds_.has_local_object("multi/c"));

    auto [fa, ra] = ds_.try_read_local("multi/a");
    EXPECT_TRUE(fa);
    CMString da(ra.data_buffer.begin(), ra.data_buffer.end());
    EXPECT_EQ(da, "data_a");

    auto [fb, rb] = ds_.try_read_local("multi/b");
    EXPECT_TRUE(fb);
    CMString db2(rb.data_buffer.begin(), rb.data_buffer.end());
    EXPECT_EQ(db2, "data_b");
}

TEST_F(DataServiceTest, TypedObjectReadableViaDataService) {
    CMString base_path = test_dir_ + "/typed_ds";
    Database db(base_path);

    db.write_object_typed("typed/ds_obj", "typed_payload", "MyType");

    auto [found, result] = ds_.try_read_local("typed/ds_obj");
    EXPECT_TRUE(found);
    EXPECT_EQ(result.py_name, "MyType");
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "typed_payload");
}

TEST_F(DataServiceTest, RemoteIdxOverwritesOnUpdate) {
    ds_.update_remote_idx("overwrite/obj", 1, "host_a", 8000);
    ds_.update_remote_idx("overwrite/obj", 2, "host_b", 9000);

    auto info = ds_.lookup_remote_idx("overwrite/obj");
    EXPECT_EQ(info.worker_id, 2u);
    EXPECT_EQ(info.host, "host_b");
    EXPECT_EQ(info.port, 9000);
}

TEST_F(DataServiceTest, RegisterDatabaseAndLocalRead) {
    CMString base_path = test_dir_ + "/reg_db";
    std::filesystem::create_directories(base_path);

    ds_.register_database("manual_db", base_path, "");

    IndexEntry entry;
    entry.object_name = "manual/obj";
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;
    entry.compression_type = 0;

    ds_.on_object_written("manual_db", "manual/obj", entry);
    ds_.on_flush("manual_db");

    EXPECT_TRUE(ds_.has_local_object("manual/obj"));
}

}
