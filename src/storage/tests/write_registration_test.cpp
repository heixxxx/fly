#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/index_entry.h>
#include <common/cpp/error_types.h>
#include <thread>
#include <chrono>
#include <filesystem>

namespace {

#define TEST_LOG(fmt, ...) fprintf(stderr, "[TEST_DEBUG] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

class WriteRegistrationTest : public ::testing::Test {
protected:
    CMString test_dir_;
    fly::DataService& ds_ = fly::DataService::instance();

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_writereg_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(WriteRegistrationTest, OnWriteStartedCreatesIncompleteEntry) {
    ds_.on_write_started("test_db", "writereg/start_obj");

    EXPECT_FALSE(ds_.has_local_object("writereg/start_obj"));

    auto [found, result] = ds_.try_read_local("writereg/start_obj");
    EXPECT_FALSE(found);
    TEST_LOG("on_write_started: entry exists but not readable (correct)");
}

TEST_F(WriteRegistrationTest, OnWriteCompletedMakesEntryReadable) {
    ds_.on_write_started("comp_db", "writereg/comp_obj");

    CMString base_path = test_dir_ + "/comp_db";
    std::filesystem::create_directories(base_path);
    ds_.register_database("comp_db", base_path, "");

    IndexEntry entry;
    entry.object_name = "writereg/comp_obj";
    entry.file_name = "test.dat";
    entry.offset = 0;
    entry.size = 5;
    entry.is_large = false;
    entry.block_count = 0;
    entry.compression_type = 0;

    CMVector<IndexEntry> entries = {entry};
    ds_.on_write_completed("comp_db", "writereg/comp_obj", entries);
    ds_.on_flush("comp_db");

    EXPECT_TRUE(ds_.has_local_object("writereg/comp_obj"));
    TEST_LOG("on_write_completed + flush: entry readable (correct)");
}

TEST_F(WriteRegistrationTest, OnWriteFailedRemovesEntry) {
    ds_.on_write_started("fail_db", "writereg/fail_obj");
    ds_.on_write_failed("fail_db", "writereg/fail_obj", "registration rejected");

    EXPECT_FALSE(ds_.has_local_object("writereg/fail_obj"));

    auto [found, result] = ds_.try_read_local("writereg/fail_obj");
    EXPECT_FALSE(found);
    TEST_LOG("on_write_failed: entry removed (correct)");
}

TEST_F(WriteRegistrationTest, WaitCompletionSucceedsForCompleteEntry) {
    CMString base_path = test_dir_ + "/wait_real_db";
    Database db(base_path);
    CMString full = db.get_obj_name("writereg/wait_real");
    std::mutex mtx;
    std::condition_variable cv;
    bool entry_created = false;

    std::thread writer([&]() {
        db.write_object("writereg/wait_real", "wait_data", false);
        ds_.drain_write_back();
        TEST_LOG("writer thread: write drained");
        {
            std::lock_guard<std::mutex> lock(mtx);
            entry_created = true;
        }
        cv.notify_all();
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]{ return entry_created; });
    }

    auto [found, result] = ds_.try_read_local_or_wait(full, 3000);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "wait_data");
    TEST_LOG("try_read_local_or_wait: returned with correct data");

    writer.join();
}

TEST_F(WriteRegistrationTest, WaitReturnsFalseForFailedEntry) {
    ds_.on_write_started("waitfail_db", "writereg/waitfail_obj");

    std::thread failer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ds_.on_write_failed("waitfail_db", "writereg/waitfail_obj", "rejected");
        TEST_LOG("failer thread: write failed");
    });

    auto [found, result] = ds_.try_read_local_or_wait("writereg/waitfail_obj", 3000);
    EXPECT_FALSE(found);
    TEST_LOG("try_read_local_or_wait: returned false for failed entry");

    failer.join();
}

TEST_F(WriteRegistrationTest, WaitTimesOutForIncompleteEntry) {
    ds_.on_write_started("timeout_db", "writereg/timeout_obj");

    auto start = std::chrono::steady_clock::now();
    auto [found, result] = ds_.try_read_local_or_wait("writereg/timeout_obj", 200);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_FALSE(found);
    EXPECT_GE(elapsed.count(), 150);
    TEST_LOG("try_read_local_or_wait: timed out after %ldms (expected ~200ms)", elapsed.count());
}

TEST_F(WriteRegistrationTest, WaitReturnsImmediatelyForCompleteEntry) {
    CMString base_path = test_dir_ + "/imm_real_db";
    Database db(base_path);
    db.write_object("writereg/imm_real", "imm_data", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("writereg/imm_real");
    auto start = std::chrono::steady_clock::now();
    auto [found, result] = ds_.try_read_local_or_wait(full, 3000);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_TRUE(found);
    EXPECT_LT(elapsed.count(), 100);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "imm_data");
    TEST_LOG("try_read_local_or_wait: immediate return for complete entry (%ldms)", elapsed.count());
}

TEST_F(WriteRegistrationTest, ConcurrentWaitersOnSameEntry) {
    CMString base_path = test_dir_ + "/conc_real_db";
    Database db(base_path);
    CMString full = db.get_obj_name("writereg/conc_real");

    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};
    std::mutex mtx;
    std::condition_variable cv;
    bool ready_to_wait = false;

    auto waiter = [&]() {
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]{ return ready_to_wait; });
        }
        auto [found, result] = ds_.try_read_local_or_wait(full, 3000);
        if (found) {
            CMString data(result.data_buffer.begin(), result.data_buffer.end());
            if (data == "conc_data") {
                success_count++;
            } else {
                fail_count++;
            }
        } else {
            fail_count++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back(waiter);
    }

    db.write_object("writereg/conc_real", "conc_data", false);
    ds_.drain_write_back();
    TEST_LOG("main: write drained, waking all waiters");
    {
        std::lock_guard<std::mutex> lock(mtx);
        ready_to_wait = true;
    }
    cv.notify_all();

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), 5);
    EXPECT_EQ(fail_count.load(), 0);
    TEST_LOG("concurrent waiters: %d success, %d fail", success_count.load(), fail_count.load());
}

TEST_F(WriteRegistrationTest, TaskErrorTypeValues) {
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::UNKNOWN), 0);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::EXECUTION_ERROR), 1);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::WRITE_TO_FROZEN_DB), 2);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::WRITE_REGISTRATION_FAILED), 3);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::WRITE_REGISTRATION_TIMEOUT), 4);
}

TEST_F(WriteRegistrationTest, FullTwoPhaseWriteViaDatabase) {
    CMString base_path = test_dir_ + "/twophase";
    Database db(base_path);

    db.write_object("twophase/obj", "hello_twophase", false);
    fly::DataService::instance().drain_write_back();

    CMString full = db.get_obj_name("twophase/obj");
    EXPECT_TRUE(ds_.has_local_object(full));

    auto [found, result] = ds_.try_read_local(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer.begin(), result.data_buffer.end());
    EXPECT_EQ(data, "hello_twophase");
    TEST_LOG("full 2-phase write via Database: read back correct data");
}

}
