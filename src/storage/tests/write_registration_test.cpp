#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/index_entry.h>
#include <common/runtime/cpp/error_types.h>
#include <common/testing/cpp/test_helpers.h>
#include <thread>
#include <chrono>
#include <filesystem>
#include <memory>

namespace {

// 写侧恒流式（T2c 2026-08-31）：write_pickle_bytes 已删（仅测试调用的过期
// API）——造数原语统一 open_write_stream → write → finish_and_commit。
static void write_raw(Database& db, const CMString& name, const CMString& data, bool backup = false) {
    std::unique_ptr<FlyStream> s(db.open_write_stream(name, "bytes"));
    ASSERT_NE(s, nullptr);
    s->write(data.data(), static_cast<size_t>(data.size()));
    ASSERT_EQ(static_cast<int>(s->finish_and_commit(backup, /*populate_cache=*/true)),
              static_cast<int>(fly::WriteErrorType::OK));
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
        test_dir_ = fly::test::qa_tmp_dir("fly_test_writereg");
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

// 2026-08-16 冗余清理：OnWriteStartedCreatesIncompleteEntry / OnWriteCompletedMakesEntryReadable /
// OnWriteFailedRemovesEntry 三用例与 data_service_test 的同名/等价用例重复，已删除；
// 本文件保留独有覆盖：TaskErrorTypeValues（错误码值契约）与 FullTwoPhaseWriteViaDatabase。

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
