#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using fly::FailedTaskRecord;

namespace {

CMString make_test_path(const CMString& name) {
    return "/tmp/fly_failed_tasks_test_" + name + ".bin";
}

void write_test_record(const CMString& file_path, const FailedTaskRecord& record) {
    CMString body;
    FLY_ENCODE(record, body);

    int64_t body_size = static_cast<int64_t>(body.size());

    std::ofstream ofs(file_path, std::ios::binary | std::ios::app);
    ofs.write(reinterpret_cast<const char*>(&body_size), sizeof(body_size));
    ofs.write(body.data(), body.size());
}

CMVector<FailedTaskRecord> read_all_records(const CMString& file_path) {
    CMVector<FailedTaskRecord> result;

    if (!std::filesystem::exists(file_path)) return result;

    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs) return result;

    while (true) {
        int64_t body_size = 0;
        ifs.read(reinterpret_cast<char*>(&body_size), sizeof(body_size));
        if (!ifs || body_size <= 0) break;

        CMString body(body_size, '\0');
        ifs.read(body.data(), body_size);
        if (!ifs) break;

        FailedTaskRecord record;
        try {
            FLY_DECODE(body, FailedTaskRecord, record);
            result.push_back(std::move(record));
        } catch (...) {}
    }

    return result;
}

class FailedTasksTest : public ::testing::Test {
protected:
    CMString file_path_;

    void SetUp() override {
        file_path_ = make_test_path(std::to_string(::getpid()) + "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove(file_path_);
    }

    void TearDown() override {
        std::filesystem::remove(file_path_);
    }

    FailedTaskRecord make_record(uint64_t id, const CMString& name) {
        FailedTaskRecord r;
        r.task_id_ = id;
        r.name_ = name;
        r.module_ = "mod_" + name;
        r.args_ = {"arg1", "arg2"};
        r.inputs_ = {"in1"};
        r.outputs_ = {"out1"};
        r.required_capabilities_ = {"gpu"};
        r.error_message_ = "failed: " + name;
        return r;
    }
};

TEST_F(FailedTasksTest, AppendAndReadSingle) {
    write_test_record(file_path_, make_record(1, "task_a"));

    auto records = read_all_records(file_path_);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].task_id_, 1u);
    EXPECT_EQ(records[0].name_, "task_a");
    EXPECT_EQ(records[0].module_, "mod_task_a");
    EXPECT_EQ(records[0].args_.size(), 2u);
    EXPECT_EQ(records[0].error_message_, "failed: task_a");
}

TEST_F(FailedTasksTest, AppendMultipleSequential) {
    write_test_record(file_path_, make_record(1, "task_a"));
    write_test_record(file_path_, make_record(2, "task_b"));
    write_test_record(file_path_, make_record(3, "task_c"));

    auto records = read_all_records(file_path_);
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].task_id_, 1u);
    EXPECT_EQ(records[1].task_id_, 2u);
    EXPECT_EQ(records[2].task_id_, 3u);
    EXPECT_EQ(records[2].name_, "task_c");
}

TEST_F(FailedTasksTest, ReadFromNonExistentFile) {
    auto records = read_all_records(file_path_);
    EXPECT_TRUE(records.empty());
}

TEST_F(FailedTasksTest, ReadFromEmptyFile) {
    std::ofstream ofs(file_path_, std::ios::binary);
    ofs.close();

    auto records = read_all_records(file_path_);
    EXPECT_TRUE(records.empty());
}

TEST_F(FailedTasksTest, RewriteAfterRemove) {
    write_test_record(file_path_, make_record(1, "task_a"));
    write_test_record(file_path_, make_record(2, "task_b"));
    write_test_record(file_path_, make_record(3, "task_c"));

    auto records = read_all_records(file_path_);
    records.erase(
        std::remove_if(records.begin(), records.end(),
            [](const FailedTaskRecord& r) { return r.task_id_ == 2; }),
        records.end());

    std::filesystem::remove(file_path_);
    for (const auto& r : records) {
        write_test_record(file_path_, r);
    }

    auto reloaded = read_all_records(file_path_);
    ASSERT_EQ(reloaded.size(), 2u);
    EXPECT_EQ(reloaded[0].task_id_, 1u);
    EXPECT_EQ(reloaded[1].task_id_, 3u);
}

TEST_F(FailedTasksTest, RewriteAllRemovedDeletesFile) {
    write_test_record(file_path_, make_record(1, "task_a"));

    auto records = read_all_records(file_path_);
    records.clear();

    std::filesystem::remove(file_path_);
    for (const auto& r : records) {
        write_test_record(file_path_, r);
    }

    EXPECT_FALSE(std::filesystem::exists(file_path_));
    auto reloaded = read_all_records(file_path_);
    EXPECT_TRUE(reloaded.empty());
}

}  // namespace
