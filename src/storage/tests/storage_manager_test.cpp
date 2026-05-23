#include <gtest/gtest.h>
#include <storage/cpp/storage_manager.h>
#include <filesystem>

namespace {

class StorageManagerTest : public ::testing::Test {
protected:
    CMVector<CMString> cleanup_dirs_;

    void SetUp() override {
        StorageManager::instance().reset();
    }

    void TearDown() override {
        StorageManager::instance().reset();
        for (const auto& dir : cleanup_dirs_) {
            std::filesystem::remove_all(dir);
        }
    }

    void track_cleanup(const CMString& path) {
        cleanup_dirs_.push_back(path);
    }
};

TEST_F(StorageManagerTest, SingletonReturnsSameInstance) {
    auto& m1 = StorageManager::instance();
    auto& m2 = StorageManager::instance();
    EXPECT_EQ(&m1, &m2);
}

TEST_F(StorageManagerTest, GetOrCreateDatabase) {
    CMString base_path = "/tmp/fly_test_sm_db1_" + std::to_string(::getpid());
    track_cleanup(base_path);
    auto db1 = StorageManager::instance().get_or_create_database(base_path, "");
    auto db2 = StorageManager::instance().get_or_create_database(base_path, "");

    EXPECT_EQ(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, DifferentPathsCreateDifferentDatabases) {
    CMString base1 = "/tmp/fly_test_sm_db2_" + std::to_string(::getpid());
    CMString base2 = "/tmp/fly_test_sm_db3_" + std::to_string(::getpid());
    track_cleanup(base1);
    track_cleanup(base2);
    auto db1 = StorageManager::instance().get_or_create_database(base1, "");
    auto db2 = StorageManager::instance().get_or_create_database(base2, "");

    EXPECT_NE(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, GetWriterByWorkerId) {
    track_cleanup("/tmp/fly_worker_1");
    auto writer1 = StorageManager::instance().get_writer(1);
    auto writer2 = StorageManager::instance().get_writer(1);

    EXPECT_EQ(writer1.get(), writer2.get());
}

TEST_F(StorageManagerTest, DifferentWorkerIdsCreateDifferentWriters) {
    track_cleanup("/tmp/fly_worker_10");
    track_cleanup("/tmp/fly_worker_20");
    auto writer1 = StorageManager::instance().get_writer(10);
    auto writer2 = StorageManager::instance().get_writer(20);

    EXPECT_NE(writer1.get(), writer2.get());
}

TEST_F(StorageManagerTest, CloseAll) {
    CMString base_path = "/tmp/fly_test_sm_close_" + std::to_string(::getpid());
    track_cleanup(base_path);
    track_cleanup("/tmp/fly_worker_1");
    StorageManager::instance().get_or_create_database(base_path, "");
    StorageManager::instance().get_writer(1);

    StorageManager::instance().close_all();
}

TEST_F(StorageManagerTest, ResetClearsCaches) {
    CMString base_path1 = "/tmp/fly_test_sm_reset1_" + std::to_string(::getpid());
    CMString base_path2 = "/tmp/fly_test_sm_reset2_" + std::to_string(::getpid());
    track_cleanup(base_path1);
    track_cleanup(base_path2);
    auto db1 = StorageManager::instance().get_or_create_database(base_path1, "");

    StorageManager::instance().reset();

    auto db2 = StorageManager::instance().get_or_create_database(base_path2, "");
    EXPECT_NE(db1.get(), db2.get());
}

}