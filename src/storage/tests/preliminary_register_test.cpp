// 注册预许可测试（chunked-transfer-design.md §14.1 阶段一，差异 #7 定案）。
//
// 语义锚定：open_write_stream 入口发 WRITE_REGISTER 预许可（许可+provenance，
// 不带 size、不激活可见性）——拒绝即失败（零序列化零落盘：begin_incremental
// 未执行、无 WBQ 单元入队）；通过后流式写正常，完成登记带真实 size。
#include <gtest/gtest.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_service.h>
#include <common/cpp/worker_context.h>
#include <common/cpp/error_types.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <fstream>

namespace {

class PreliminaryRegisterTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<fly::DataService> ds_ = fly::DataService::instance();
    // register 调用记录（preliminary 与完成登记两类）。
    CMVector<std::pair<bool, int64_t>> calls_;  // (preliminary, size)

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_prereg_" + std::to_string(::getpid()) + "_" +
                    std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        ds_->reset();
        fly::WorkerAgentContext::clear();
    }

    void TearDown() override {
        fly::WorkerAgentContext::clear();
        std::filesystem::remove_all(test_dir_);
    }
};

}  // namespace

// 预许可被拒（DUPLICATE）→ open_write_stream 返回 nullptr，零副作用。
TEST_F(PreliminaryRegisterTest, RejectedPreliminaryFailsFast) {
    fly::WorkerAgentContext::set_register_func(
        [this](const fly::CMString&, const fly::CMString&, int64_t size, bool preliminary)
            -> std::pair<fly::CMString, fly::TaskErrorType> {
            calls_.push_back({preliminary, size});
            if (preliminary) {
                return {"duplicate", fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED};
            }
            return {"", fly::TaskErrorType::UNKNOWN};
        });

    Database db(test_dir_, test_dir_ + "/data");
    FlyStream* s = db.open_write_stream("obj", "bytes");
    EXPECT_EQ(s, nullptr) << "rejected preliminary must fail open_write_stream";
    ASSERT_EQ(calls_.size(), 1u);
    EXPECT_TRUE(calls_[0].first) << "only the preliminary call should have happened";
    EXPECT_EQ(calls_[0].second, 0) << "preliminary carries no size";
    EXPECT_EQ(fly::WorkerAgentContext::get_last_error_type(),
              fly::TaskErrorType::WRITE_DUPLICATE_SKIPPED);
}

// 预许可通过 → 流正常；完成登记带真实 size（非 preliminary）。
TEST_F(PreliminaryRegisterTest, AcceptedPreliminaryThenCompletionRegister) {
    fly::WorkerAgentContext::set_register_func(
        [this](const fly::CMString&, const fly::CMString&, int64_t size, bool preliminary)
            -> std::pair<fly::CMString, fly::TaskErrorType> {
            calls_.push_back({preliminary, size});
            return {"", fly::TaskErrorType::UNKNOWN};
        });

    Database db(test_dir_, test_dir_ + "/data");
    FlyStream* s = db.open_write_stream("obj", "bytes");
    ASSERT_NE(s, nullptr);
    std::string payload(1000, 'P');
    s->write(payload.data(), payload.size());
    int64_t err = s->finish_and_commit(false, true);
    EXPECT_EQ(err, static_cast<int64_t>(fly::WriteErrorType::OK));

    ASSERT_EQ(calls_.size(), 2u);
    EXPECT_TRUE(calls_[0].first);
    EXPECT_EQ(calls_[0].second, 0);
    EXPECT_FALSE(calls_[1].first) << "completion register is not preliminary";
    EXPECT_GT(calls_[1].second, 0) << "completion register carries real size";

    delete s;
}
