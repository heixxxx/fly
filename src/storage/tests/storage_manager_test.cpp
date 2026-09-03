#include <gtest/gtest.h>
#include <storage/cpp/storage_manager.h>
#include <storage/cpp/data_service.h>
#include <common/cpp/test_helpers.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <latch>
#include <mutex>
#include <thread>
#include <utility>

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
    CMString db_path = fly::test::qa_tmp_dir("fly_test_sm_db1");
    track_cleanup(db_path);
    auto db1 = StorageManager::instance()->get_or_create_database(db_path, "");
    auto db2 = StorageManager::instance()->get_or_create_database(db_path, "");

    EXPECT_EQ(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, DifferentPathsCreateDifferentDatabases) {
    CMString base1 = fly::test::qa_tmp_dir("fly_test_sm_db2");
    CMString base2 = fly::test::qa_tmp_dir("fly_test_sm_db3");
    track_cleanup(base1);
    track_cleanup(base2);
    auto db1 = StorageManager::instance()->get_or_create_database(base1, "");
    auto db2 = StorageManager::instance()->get_or_create_database(base2, "");

    EXPECT_NE(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, CloseAll) {
    CMString db_path = fly::test::qa_tmp_dir("fly_test_sm_close");
    track_cleanup(db_path);
    StorageManager::instance()->get_or_create_database(db_path, "");

    StorageManager::instance()->close_all();
}

TEST_F(StorageManagerTest, ResetClearsCaches) {
    CMString db_path1 = fly::test::qa_tmp_dir("fly_test_sm_reset1");
    CMString db_path2 = fly::test::qa_tmp_dir("fly_test_sm_reset2");
    track_cleanup(db_path1);
    track_cleanup(db_path2);
    auto db1 = StorageManager::instance()->get_or_create_database(db_path1, "");

    StorageManager::instance()->reset();

    auto db2 = StorageManager::instance()->get_or_create_database(db_path2, "");
    EXPECT_NE(db1.get(), db2.get());
}

TEST_F(StorageManagerTest, GetOrCreateDatabaseCreatesDataPathDirectory) {
    CMString db_path = fly::test::qa_tmp_dir("fly_test_sm_datapath");
    CMString data_path = db_path + "/data";
    track_cleanup(db_path);
    auto db = StorageManager::instance()->get_or_create_database(db_path, data_path);

    EXPECT_TRUE(std::filesystem::exists(data_path));
    EXPECT_TRUE(std::filesystem::is_directory(data_path));
}

TEST_F(StorageManagerTest, GetOrCreateDatabaseCacheHitWithAndWithoutDataPath) {
    CMString db_path = fly::test::qa_tmp_dir("fly_test_sm_cache");
    track_cleanup(db_path);
    auto db1 = StorageManager::instance()->get_or_create_database(db_path, "");
    auto db2 = StorageManager::instance()->get_or_create_database(db_path, "data");
    auto db3 = StorageManager::instance()->get_or_create_database(db_path, "");

    EXPECT_EQ(db1.get(), db2.get());
    EXPECT_EQ(db2.get(), db3.get());
}

TEST_F(StorageManagerTest, CloseAllFreezesDatabase) {
    CMString db_path = fly::test::qa_tmp_dir("fly_test_sm_close");
    track_cleanup(db_path);

    auto db = StorageManager::instance()->get_or_create_database(db_path, "");

    StorageManager::instance()->close_all();

    EXPECT_TRUE(db->is_frozen());
}

TEST_F(StorageManagerTest, ResetUnregistersDatabaseFromDataService) {
    CMString db_path = fly::test::qa_tmp_dir("fly_test_sm_resetds");
    track_cleanup(db_path);

    auto db = StorageManager::instance()->get_or_create_database(db_path, "");

    auto ds = fly::DataService::instance();
    EXPECT_TRUE(ds->has_database(db->get_db_path()));

    StorageManager::instance()->reset();

    EXPECT_FALSE(ds->has_database(db->get_db_path()));
}

// ── 并发正确性（§13.3 快照模式回归）：Database 构造/freeze 是重 IO，
//    容器锁内只允许纯 map 操作——并发下不死锁、winner 语义唯一、
//    close_all 后全量冻结。 ──

TEST_F(StorageManagerTest, ConcurrentGetOrCreateDistinctPaths) {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 6;
    CMVector<CMString> paths;
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kPerThread; ++i) {
            CMString p = fly::test::qa_tmp_dir(
                "fly_test_sm_conc_" + std::to_string(t) + "_" + std::to_string(i));
            track_cleanup(p);
            paths.push_back(p);
        }
    }
    std::latch go{kThreads};
    CMVector<CMSharedPtr<Database>> results(paths.size());
    std::atomic<int> next{0};
    CMVector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            go.count_down();
            go.wait();
            for (int i = 0; i < kPerThread; ++i) {
                int idx = next.fetch_add(1);
                results[idx] = StorageManager::instance()->get_or_create_database(paths[idx], "");
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (size_t i = 0; i < paths.size(); ++i) {
        ASSERT_NE(results[i], nullptr) << "path idx " << i;
        auto again = StorageManager::instance()->get_or_create_database(paths[i], "");
        EXPECT_EQ(results[i].get(), again.get()) << "path idx " << i;
    }
    StorageManager::instance()->close_all();
    for (size_t i = 0; i < paths.size(); ++i) {
        EXPECT_TRUE(results[i]->is_frozen()) << "path idx " << i;
    }
}

TEST_F(StorageManagerTest, ConcurrentGetOrCreateSamePathReturnsSingleInstance) {
    CMString db_path = fly::test::qa_tmp_dir("fly_test_sm_conc_same");
    track_cleanup(db_path);
    constexpr int kThreads = 8;
    std::latch go{kThreads};
    CMVector<CMSharedPtr<Database>> results(kThreads);
    CMVector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            go.count_down();
            go.wait();
            results[t] = StorageManager::instance()->get_or_create_database(db_path, "");
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (int t = 1; t < kThreads; ++t) {
        EXPECT_EQ(results[0].get(), results[t].get()) << "thread " << t;
    }
    StorageManager::instance()->close_all();
}

TEST_F(StorageManagerTest, CloseAllConcurrentWithGetOrCreate) {
    constexpr int kCreators = 3;
    constexpr int kPerCreator = 8;
    std::atomic<bool> stop{false};
    std::mutex all_mutex;
    CMVector<std::pair<CMString, CMSharedPtr<Database>>> created;
    std::latch go{kCreators + 1};
    CMVector<std::thread> threads;
    for (int t = 0; t < kCreators; ++t) {
        threads.emplace_back([&, t] {
            go.count_down();
            go.wait();
            for (int i = 0; i < kPerCreator && !stop.load(); ++i) {
                CMString p = fly::test::qa_tmp_dir(
                    "fly_test_sm_race_" + std::to_string(t) + "_" + std::to_string(i));
                auto db = StorageManager::instance()->get_or_create_database(p, "");
                std::lock_guard<std::mutex> lk(all_mutex);
                created.emplace_back(p, db);
            }
        });
    }
    go.count_down();
    go.wait();
    for (int r = 0; r < 4 && !stop.load(); ++r) {
        StorageManager::instance()->close_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    stop.store(true);
    for (auto& th : threads) {
        th.join();
    }
    for (const auto& [p, db] : created) {
        track_cleanup(p);
    }
    // 终局收口：所有创建实例此刻必在容器内 → 全量冻结（close_all 与
    // get_or_create 并发交错后仍收敛）。
    StorageManager::instance()->close_all();
    for (const auto& [p, db] : created) {
        EXPECT_TRUE(db->is_frozen()) << "path " << p;
    }
}

}