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

// db_path 废弃：db_path 现在是 db_path 别名（不含 ':'）。db32 生成不含 ':' 的测试 db_path。
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
    CMString db_path = test_dir_ + "/comp_db";
    std::filesystem::create_directories(db_path);
    CMString full = db_path + ":comp_obj";

    ds_->on_write_started(db_path, full);

    ds_->register_database(db_path, "");

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

TEST_F(WriteRegistrationTest, TaskErrorTypeValues) {
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::UNKNOWN), 0);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::EXECUTION_ERROR), 1);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::WRITE_TO_FROZEN_DB), 2);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::WRITE_REGISTRATION_FAILED), 3);
    EXPECT_EQ(static_cast<int>(fly::TaskErrorType::WRITE_REGISTRATION_TIMEOUT), 4);
}

TEST_F(WriteRegistrationTest, FullTwoPhaseWriteViaDatabase) {
    CMString db_path = test_dir_ + "/twophase";
    Database db(db_path);

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
