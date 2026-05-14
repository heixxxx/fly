#include <gtest/gtest.h>
#include <storage/cpp/data_writer.h>
#include <filesystem>
#include <fstream>

namespace {

class DataWriterTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_data_writer_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DataWriterTest, WriteSmallObject) {
    CMString base_path = test_dir_ + "/base";
    DataWriter writer(base_path, "", 1, 1024, 10240, 128);

    CMString file = writer.write_object("small/test", "hello world", false);
    EXPECT_FALSE(file.empty());

    EXPECT_GT(writer.total_bytes_written(), 0);
    writer.close();
}

TEST_F(DataWriterTest, WriteMultipleSmallObjects) {
    CMString base_path = test_dir_ + "/multi_base";
    DataWriter writer(base_path, "", 1, 1024, 10240, 128);

    for (int i = 0; i < 5; i++) {
        CMString name = "obj_" + std::to_string(i);
        CMString data = "data_" + std::to_string(i);
        writer.write_object(name, data, false);
    }

    EXPECT_EQ(writer.total_bytes_written(), 30);
    writer.close();
}

TEST_F(DataWriterTest, AggregationThresholdCreatesNewFile) {
    CMString base_path = test_dir_ + "/agg_base";
    DataWriter writer(base_path, "", 1, 50, 10240, 128);

    CMString data1(30, 'a');
    writer.write_object("obj1", data1, false);

    CMString data2(30, 'b');
    writer.write_object("obj2", data2, false);

    EXPECT_EQ(writer.file_count(), 2);
    writer.close();
}

TEST_F(DataWriterTest, WriteLargeObject) {
    CMString base_path = test_dir_ + "/large_base";
    DataWriter writer(base_path, "", 1, 1048576, 100, 50);

    CMString large_data(500, 'x');
    CMString file = writer.write_object("large/test", large_data, false);
    EXPECT_FALSE(file.empty());

    EXPECT_EQ(writer.total_bytes_written(), 500);
    writer.close();
}

TEST_F(DataWriterTest, FlushPersistsIndex) {
    CMString base_path = test_dir_ + "/flush_base";
    CMString idx_path = base_path + "/worker_1.idx";

    {
        DataWriter writer(base_path, "", 1, 1024, 10240, 128);
        writer.write_object("flush/obj", "data", false);
        writer.flush();

        std::ifstream ifs(idx_path, std::ios::binary);
        EXPECT_TRUE(ifs.good());
    }
}

TEST_F(DataWriterTest, CloseIsIdempotent) {
    CMString base_path = test_dir_ + "/close_base";
    DataWriter writer(base_path, "", 1, 1024, 10240, 128);

    writer.write_object("close/obj", "data", false);
    writer.close();
    writer.close();
}

TEST_F(DataWriterTest, WriteAfterCloseThrows) {
    CMString base_path = test_dir_ + "/closed_base";
    DataWriter writer(base_path, "", 1, 1024, 10240, 128);

    writer.write_object("obj", "data", false);
    writer.close();

    EXPECT_THROW(writer.write_object("obj2", "data2", false), std::runtime_error);
}

TEST_F(DataWriterTest, WriteWithCustomDataPath) {
    CMString base_path = test_dir_ + "/base_custom";
    CMString data_path = test_dir_ + "/data_custom";
    DataWriter writer(base_path, data_path, 1, 1024, 10240, 128);

    CMString file = writer.write_object("custom/obj", "hello", false);
    EXPECT_FALSE(file.empty());

    std::filesystem::path dp(data_path);
    EXPECT_TRUE(std::filesystem::exists(dp));

    writer.close();
}

TEST_F(DataWriterTest, FileCountIncrements) {
    CMString base_path = test_dir_ + "/count_base";
    DataWriter writer(base_path, "", 1, 20, 10240, 128);

    EXPECT_EQ(writer.file_count(), 1);

    CMString data1(15, 'a');
    writer.write_object("obj1", data1, false);
    EXPECT_EQ(writer.file_count(), 1);

    CMString data2(15, 'b');
    writer.write_object("obj2", data2, false);
    EXPECT_EQ(writer.file_count(), 2);

    writer.close();
}

}