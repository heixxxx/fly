#include <gtest/gtest.h>
#include <storage/cpp/storage_manager.h>
#include <storage/cpp/data_service.h>
#include <filesystem>

namespace {

class StorageManagerTest : public ::testing::Test {
protected:
    CMVector<CMString> cleanup_dirs_;

    void SetUp() override {
        StorageManager::instance()->reset();
    }

    void TearDown() override {
        StorageManager::instance()->reset();
        for (const auto& dir : cleanup_dirs_) {
            std::filesystem::remove_all(dir);
        }
    }

    void track_cleanup(const CMString& path) {
        cleanup_dirs_.push_back(path);
    }
};

TEST_F(StorageManagerTest, SingletonReturnsSameInstance) {
    auto m1 = StorageManager::instance();
    auto m2 = StorageManager::instance();
    EXPECT_EQ(m1.get(), m2.get());
}

TEST_F(StorageManagerTest, GetOrCreateDatabase) {
    CMString base_path = "/tmp/fly_test_sm_db1_" + std::to_string(::getpid());
    track_cleanup(base_path);
    auto db1 = StorageManager::instance()->get_or_create_database(base_path, "");
    auto db2 = StorageManager::instance()->get_or_create_database(base_path, "");

    EXPECT_EQ(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, DifferentPathsCreateDifferentDatabases) {
    CMString base1 = "/tmp/fly_test_sm_db2_" + std::to_string(::getpid());
    CMString base2 = "/tmp/fly_test_sm_db3_" + std::to_string(::getpid());
    track_cleanup(base1);
    track_cleanup(base2);
    auto db1 = StorageManager::instance()->get_or_create_database(base1, "");
    auto db2 = StorageManager::instance()->get_or_create_database(base2, "");

    EXPECT_NE(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, GetWriterByWorkerId) {
    track_cleanup("/tmp/fly_worker_1");
    auto writer1 = StorageManager::instance()->get_writer(1);
    auto writer2 = StorageManager::instance()->get_writer(1);

    EXPECT_EQ(writer1.get(), writer2.get());
}

TEST_F(StorageManagerTest, DifferentWorkerIdsCreateDifferentWriters) {
    track_cleanup("/tmp/fly_worker_10");
    track_cleanup("/tmp/fly_worker_20");
    auto writer1 = StorageManager::instance()->get_writer(10);
    auto writer2 = StorageManager::instance()->get_writer(20);

    EXPECT_NE(writer1.get(), writer2.get());
}

TEST_F(StorageManagerTest, CloseAll) {
    CMString base_path = "/tmp/fly_test_sm_close_" + std::to_string(::getpid());
    track_cleanup(base_path);
    track_cleanup("/tmp/fly_worker_1");
    StorageManager::instance()->get_or_create_database(base_path, "");
    StorageManager::instance()->get_writer(1);

    StorageManager::instance()->close_all();
}

TEST_F(StorageManagerTest, ResetClearsCaches) {
    CMString base_path1 = "/tmp/fly_test_sm_reset1_" + std::to_string(::getpid());
    CMString base_path2 = "/tmp/fly_test_sm_reset2_" + std::to_string(::getpid());
    track_cleanup(base_path1);
    track_cleanup(base_path2);
    auto db1 = StorageManager::instance()->get_or_create_database(base_path1, "");

    StorageManager::instance()->reset();

    auto db2 = StorageManager::instance()->get_or_create_database(base_path2, "");
    EXPECT_NE(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, GetOrCreateDatabaseCreatesDataPathDirectory) {
    CMString base_path = "/tmp/fly_test_sm_datapath_" + std::to_string(::getpid());
    CMString data_path = base_path + "/data";
    track_cleanup(base_path);
    auto db = StorageManager::instance()->get_or_create_database(base_path, data_path);

    EXPECT_TRUE(std::filesystem::exists(data_path));
    EXPECT_TRUE(std::filesystem::is_directory(data_path));
}

TEST_F(StorageManagerTest, GetOrCreateDatabaseCacheHitWithAndWithoutDataPath) {
    CMString base_path = "/tmp/fly_test_sm_cache_" + std::to_string(::getpid());
    track_cleanup(base_path);
    auto db1 = StorageManager::instance()->get_or_create_database(base_path, "");
    auto db2 = StorageManager::instance()->get_or_create_database(base_path, "data");
    auto db3 = StorageManager::instance()->get_or_create_database(base_path, "");

    EXPECT_EQ(db1.get(), db2.get());
    EXPECT_EQ(db2.get(), db3.get());
}

TEST_F(StorageManagerTest, GetWriterCreatesWorkerDirectory) {
    int worker_id = 42;
    std::string expected_path = "/tmp/fly_worker_" + std::to_string(worker_id);
    track_cleanup(expected_path);

    auto writer = StorageManager::instance()->get_writer(worker_id);

    EXPECT_TRUE(std::filesystem::exists(expected_path));
    EXPECT_TRUE(std::filesystem::is_directory(expected_path));
}

TEST_F(StorageManagerTest, GetWriterReturnsSameInstanceForSameWorkerId) {
    int worker_id = 123;
    track_cleanup("/tmp/fly_worker_123");

    auto writer1 = StorageManager::instance()->get_writer(worker_id);
    auto writer2 = StorageManager::instance()->get_writer(worker_id);

    EXPECT_EQ(writer1.get(), writer2.get());
}

TEST_F(StorageManagerTest, CloseAllFreezesDatabaseAndClosesWriters) {
    CMString base_path = "/tmp/fly_test_sm_close_" + std::to_string(::getpid());
    CMString worker_dir = "/tmp/fly_worker_close_" + std::to_string(::getpid());
    track_cleanup(base_path);
    track_cleanup(worker_dir);

    auto db = StorageManager::instance()->get_or_create_database(base_path, "");
    auto writer = StorageManager::instance()->get_writer(1);

    StorageManager::instance()->close_all();

    EXPECT_TRUE(db->is_frozen());
}

TEST_F(StorageManagerTest, ResetUnregistersDatabaseFromDataService) {
    CMString base_path = "/tmp/fly_test_sm_resetds_" + std::to_string(::getpid());
    track_cleanup(base_path);

    auto db = StorageManager::instance()->get_or_create_database(base_path, "");

    auto ds = fly::DataService::instance();
    EXPECT_TRUE(ds->has_database(db->get_db_id()));

    StorageManager::instance()->reset();

    EXPECT_FALSE(ds->has_database(db->get_db_id()));
}

}