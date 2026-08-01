#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/index_entry.h>
#include <common/cpp/error_types.h>
#include <thread>
#include <chrono>
#include <filesystem>

namespace {

static void write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    db.write_pickle_bytes(name, data.data(), static_cast<int64_t>(data.size()), "bytes", backup);
}

#define TEST_LOG(fmt, ...) fprintf(stderr, "[TEST_DEBUG] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

// db_path 废弃：db_path 现在是 base_path 别名（不含 ':'）。db32 生成不含 ':' 的测试 db_path。
static CMString db32(const CMString& hint) {
    return "/test/" + hint;
}

class WriteRegistrationTest : public ::testing::Test {
protected:
    CMString test_dir_;
    fly::CMSharedPtr<fly::DataService> ds_ = fly::DataService::instance();

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
    CMString db_path = db32("test_db");
    CMString full = db_path + ":start_obj";
    ds_->on_write_started(db_path, full);

    EXPECT_FALSE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_FALSE(found);
    TEST_LOG("on_write_started: entry exists but not readable (correct)");
}

TEST_F(WriteRegistrationTest, OnWriteCompletedMakesEntryReadable) {
    CMString db_path = db32("comp_db");
    CMString full = db_path + ":comp_obj";

    ds_->on_write_started(db_path, full);

    CMString base_path = test_dir_ + "/comp_db";
    std::filesystem::create_directories(base_path);
    ds_->register_database(db_path, base_path, "");

    IndexEntry entry;
    entry.object_name_ = full;
    entry.file_name_ = "test.dat";
    entry.offset_ = 0;
    entry.size_ = 5;
    entry.is_large_ = false;
    entry.block_count_ = 0;

    CMVector<IndexEntry> entries = {entry};
    ds_->on_write_completed(db_path, full, entries);
    ds_->on_flush(db_path);

    EXPECT_TRUE(ds_->has_local_object(full));
    TEST_LOG("on_write_completed + flush: entry readable (correct)");
}

TEST_F(WriteRegistrationTest, OnWriteFailedRemovesEntry) {
    CMString db_path = db32("fail_db");
    CMString full = db_path + ":fail_obj";

    ds_->on_write_started(db_path, full);
    ds_->on_write_failed(db_path, full, "registration rejected");

    EXPECT_FALSE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_FALSE(found);
    TEST_LOG("on_write_failed: entry removed (correct)");
}

TEST_F(WriteRegistrationTest, WaitCompletionSucceedsForCompleteEntry) {
    CMString base_path = test_dir_ + "/wait_real_db";
    Database db(base_path);
    CMString full = db.get_full_name("writereg/wait_real");
    std::mutex mtx;
    std::condition_variable cv;
    bool entry_created = false;

    std::thread writer([&]() {
        write_raw(db, "writereg/wait_real", "wait_data", false);
        ds_->drain_write_back();
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

    auto [found, result] = ds_->try_read_local_or_wait(full, 3000);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "wait_data");
    TEST_LOG("try_read_local_or_wait: returned with correct data");

    writer.join();
}

TEST_F(WriteRegistrationTest, WaitReturnsFalseForFailedEntry) {
    CMString db_path = db32("waitfail_db");
    CMString full = db_path + ":waitfail_obj";

    ds_->on_write_started(db_path, full);

    std::thread failer([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        ds_->on_write_failed(db_path, full, "rejected");
        TEST_LOG("failer thread: write failed");
    });

    auto [found, result] = ds_->try_read_local_or_wait(full, 3000);
    EXPECT_FALSE(found);
    TEST_LOG("try_read_local_or_wait: returned false for failed entry");

    failer.join();
}

TEST_F(WriteRegistrationTest, WaitTimesOutForIncompleteEntry) {
    CMString db_path = db32("timeout_db");
    CMString full = db_path + ":timeout_obj";

    ds_->on_write_started(db_path, full);

    auto start = std::chrono::steady_clock::now();
    auto [found, result] = ds_->try_read_local_or_wait(full, 200);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_FALSE(found);
    EXPECT_GE(elapsed.count(), 150);
    TEST_LOG("try_read_local_or_wait: timed out after %ldms (expected ~200ms)", elapsed.count());
}

TEST_F(WriteRegistrationTest, WaitReturnsImmediatelyForCompleteEntry) {
    CMString base_path = test_dir_ + "/imm_real_db";
    Database db(base_path);
    write_raw(db, "writereg/imm_real", "imm_data", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("writereg/imm_real");
    auto start = std::chrono::steady_clock::now();
    auto [found, result] = ds_->try_read_local_or_wait(full, 3000);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_TRUE(found);
    EXPECT_LT(elapsed.count(), 100);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "imm_data");
    TEST_LOG("try_read_local_or_wait: immediate return for complete entry (%ldms)", elapsed.count());
}

TEST_F(WriteRegistrationTest, ConcurrentWaitersOnSameEntry) {
    CMString base_path = test_dir_ + "/conc_real_db";
    Database db(base_path);
    CMString full = db.get_full_name("writereg/conc_real");

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
        auto [found, result] = ds_->try_read_local_or_wait(full, 3000);
        if (found) {
            CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
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

    write_raw(db, "writereg/conc_real", "conc_data", false);
    ds_->drain_write_back();
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

    write_raw(db, "twophase/obj", "hello_twophase", false);
    fly::DataService::instance()->drain_write_back();

    CMString full = db.get_full_name("twophase/obj");
    EXPECT_TRUE(ds_->has_local_object(full));

    auto [found, result] = ds_->try_read_local(full);
    EXPECT_TRUE(found);
    CMString data(result.data_buffer_.begin(), result.data_buffer_.end());
    EXPECT_EQ(data, "hello_twophase");
    TEST_LOG("full 2-phase write via Database: read back correct data");
}

}
