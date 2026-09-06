#include <gtest/gtest.h>
#include <agent/cpp/master_agent.h>
#include <common/testing/cpp/test_helpers.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using fly::FailedTaskRecord;

namespace {

CMString make_test_path(const CMString& name) {
    // qa_tmp_dir 只生成路径不建目录；此处产物是文件，先建父目录
    //（手动直跑时 fallback 基目录 .work/gtest_tmp 可能不存在）。
    CMString path = fly::test::qa_tmp_dir("fly_failed_tasks_test_" + name);
    std::filesystem::create_directories(path);
    return path + ".bin";
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
        r.submission_.name_ = name;
        r.submission_.module_ = "mod_" + name;
        r.submission_.args_ = {"arg1", "arg2"};
        r.submission_.inputs_ = {"in1"};
        r.submission_.outputs_ = {"out1"};
        r.submission_.required_capabilities_ = {"gpu"};
        r.submission_.attribute_timeout_ = 2.5f;  // 限时降级（非默认 -1.0 死等）
        r.submission_.priority_ = 20;             // 高优先级（非默认 10）
        r.error_message_ = "failed: " + name;
        return r;
    }
};

TEST_F(FailedTasksTest, AppendAndReadSingle) {
    write_test_record(file_path_, make_record(1, "task_a"));

    auto records = read_all_records(file_path_);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].task_id_, 1u);
    EXPECT_EQ(records[0].submission_.name_, "task_a");
    EXPECT_EQ(records[0].submission_.module_, "mod_task_a");
    EXPECT_EQ(records[0].submission_.args_.size(), 2u);
    EXPECT_EQ(records[0].error_message_, "failed: task_a");
    // priority + attribute_timeout 必须在序列化往返后保留（restart 恢复依赖）
    EXPECT_EQ(records[0].submission_.priority_, 20);
    EXPECT_FLOAT_EQ(records[0].submission_.attribute_timeout_, 2.5f);
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
    EXPECT_EQ(records[2].submission_.name_, "task_c");
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

// ── priority + attribute_timeout 持久化往返（崩溃恢复 restart 不丢调度参数）──
// 回归保护：FailedTaskRecord 必须完整保留 priority_ 和 attribute_timeout_，
// 否则 @as_task(priority=20, requires=(["gpu"], 5.0)) 失败重启后会退化为
// priority=10 + 死等 gpu，调度语义被破坏。

TEST_F(FailedTasksTest, PriorityAndTimeoutSurviveRoundTrip) {
    // 三档 priority + 三档 attribute_timeout 的组合，全部用非默认值
    FailedTaskRecord high_prio_soft_gpu;
    high_prio_soft_gpu.task_id_ = 1;
    high_prio_soft_gpu.submission_.name_ = "high_soft";
    high_prio_soft_gpu.submission_.required_capabilities_ = {"gpu"};
    high_prio_soft_gpu.submission_.attribute_timeout_ = 5.0f;   // 限时降级
    high_prio_soft_gpu.submission_.priority_ = 20;              // 抢先

    FailedTaskRecord low_prio_background;
    low_prio_background.task_id_ = 2;
    low_prio_background.submission_.name_ = "low_bg";
    low_prio_background.submission_.attribute_timeout_ = -1.0f;  // 死等
    low_prio_background.submission_.priority_ = 1;               // 让路

    FailedTaskRecord immediate_degrade;
    immediate_degrade.task_id_ = 3;
    immediate_degrade.submission_.name_ = "immediate";
    immediate_degrade.submission_.required_capabilities_ = {"tpu"};
    immediate_degrade.submission_.attribute_timeout_ = 0.0f;     // 立即降级
    immediate_degrade.submission_.priority_ = 15;

    write_test_record(file_path_, high_prio_soft_gpu);
    write_test_record(file_path_, low_prio_background);
    write_test_record(file_path_, immediate_degrade);

    auto records = read_all_records(file_path_);
    ASSERT_EQ(records.size(), 3u);

    EXPECT_EQ(records[0].submission_.priority_, 20);
    EXPECT_FLOAT_EQ(records[0].submission_.attribute_timeout_, 5.0f);
    EXPECT_EQ(records[0].submission_.required_capabilities_, CMVector<CMString>{"gpu"});

    EXPECT_EQ(records[1].submission_.priority_, 1);
    EXPECT_FLOAT_EQ(records[1].submission_.attribute_timeout_, -1.0f);

    EXPECT_EQ(records[2].submission_.priority_, 15);
    EXPECT_FLOAT_EQ(records[2].submission_.attribute_timeout_, 0.0f);
    EXPECT_EQ(records[2].submission_.required_capabilities_, CMVector<CMString>{"tpu"});
}

TEST_F(FailedTasksTest, DefaultPriorityAndTimeoutWhenUnset) {
    // 默认构造的 record（不显式设置 priority/timeout）应保留默认值
    FailedTaskRecord r;
    r.task_id_ = 42;
    r.submission_.name_ = "default_vals";
    write_test_record(file_path_, r);

    auto records = read_all_records(file_path_);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].submission_.priority_, 10);              // 默认 priority
    EXPECT_FLOAT_EQ(records[0].submission_.attribute_timeout_, -1.0f);  // 默认死等
}

TEST_F(FailedTasksTest, OwnerDbPathSurviveRoundTrip) {
    // 归属 db 路径必须在序列化往返后保留：restart 重投按它落回同一归属
    // db 目录的 bin（persist_failed_task 读 submission_.owner_db_path_）。
    FailedTaskRecord r1;
    r1.task_id_ = 1;
    r1.submission_.name_ = "owned";
    r1.submission_.owner_db_path_ = "/proj/matrix";

    FailedTaskRecord r2;  // 无归属 task：owner 保持空（fallback log_dir 语义）
    r2.task_id_ = 2;
    r2.submission_.name_ = "unowned";

    write_test_record(file_path_, r1);
    write_test_record(file_path_, r2);

    auto records = read_all_records(file_path_);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].submission_.owner_db_path_, "/proj/matrix");
    EXPECT_EQ(records[1].submission_.owner_db_path_, "");
}

}  // namespace
