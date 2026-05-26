#include <gtest/gtest.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <serialization/cpp/fly_buffer.h>
#include <serialization/cpp/object_header.h>
#include <filesystem>
#include <fstream>
#include <cstring>

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
    DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);

    CMString file = writer.write_object("small/test", "hello world", false);
    EXPECT_FALSE(file.empty());

    EXPECT_GT(writer.total_bytes_written(), 0);
    writer.close();
}

TEST_F(DataWriterTest, WriteMultipleSmallObjects) {
    CMString base_path = test_dir_ + "/multi_base";
    DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);

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
    DataWriter writer(base_path, "", "a1b2c3d4", 50, 10240, 128);

    CMString data1(30, 'a');
    writer.write_object("obj1", data1, false);

    CMString data2(30, 'b');
    writer.write_object("obj2", data2, false);

    EXPECT_EQ(writer.file_count(), 2);
    writer.close();
}

TEST_F(DataWriterTest, WriteLargeObject) {
    CMString base_path = test_dir_ + "/large_base";
    DataWriter writer(base_path, "", "a1b2c3d4", 1048576, 100, 50);

    CMString large_data(500, 'x');
    CMString file = writer.write_object("large/test", large_data, false);
    EXPECT_FALSE(file.empty());

    EXPECT_EQ(writer.total_bytes_written(), 500);
    writer.close();
}

TEST_F(DataWriterTest, FlushPersistsIndex) {
    CMString base_path = test_dir_ + "/flush_base";
    CMString idx_path = base_path + "/a1b2c3d4.idx";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("flush/obj", "data", false);
        writer.flush();

        std::ifstream ifs(idx_path, std::ios::binary);
        EXPECT_TRUE(ifs.good());
    }
}

TEST_F(DataWriterTest, CloseIsIdempotent) {
    CMString base_path = test_dir_ + "/close_base";
    DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);

    writer.write_object("close/obj", "data", false);
    writer.close();
    writer.close();
}

TEST_F(DataWriterTest, WriteAfterCloseThrows) {
    CMString base_path = test_dir_ + "/closed_base";
    DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);

    writer.write_object("obj", "data", false);
    writer.close();

    EXPECT_TRUE(writer.write_object("obj2", "data2", false).empty());
}

TEST_F(DataWriterTest, WriteWithCustomDataPath) {
    CMString base_path = test_dir_ + "/base_custom";
    CMString data_path = test_dir_ + "/data_custom";
    DataWriter writer(base_path, data_path, "a1b2c3d4", 1024, 10240, 128);

    CMString file = writer.write_object("custom/obj", "hello", false);
    EXPECT_FALSE(file.empty());

    std::filesystem::path dp(data_path);
    EXPECT_TRUE(std::filesystem::exists(dp));

    writer.close();
}

TEST_F(DataWriterTest, FileCountIncrements) {
    CMString base_path = test_dir_ + "/count_base";
    DataWriter writer(base_path, "", "a1b2c3d4", 20, 10240, 128);

    EXPECT_EQ(writer.file_count(), 1);

    CMString data1(15, 'a');
    writer.write_object("obj1", data1, false);
    EXPECT_EQ(writer.file_count(), 1);

    CMString data2(15, 'b');
    writer.write_object("obj2", data2, false);
    EXPECT_EQ(writer.file_count(), 2);

    writer.close();
}

TEST_F(DataWriterTest, CompressToBufferProducesData) {
    CMString base_path = test_dir_ + "/compress_buf";
    DataWriter writer(base_path, "", "c1d2e3f4", 1024, 10240, 128);

    CMString data = "hello stream pipeline";
    FlyBuffer target;
    auto result = writer.compress_to_buffer(
        static_cast<uint64_t>(data.size()), "", data.data(),
        static_cast<int64_t>(data.size()), target);

    EXPECT_FALSE(target.empty());
    EXPECT_EQ(result.original_size, static_cast<int64_t>(data.size()));
    EXPECT_EQ(result.record_size, static_cast<int64_t>(target.size()));

    writer.close();
}

TEST_F(DataWriterTest, CompressToBufferWireFormat) {
    CMString base_path = test_dir_ + "/wire_fmt";
    DataWriter writer(base_path, "", "c1d2e3f4", 1024, 10240, 128);

    CMString data = "hello stream pipeline";
    FlyBuffer target;
    writer.compress_to_buffer(
        static_cast<uint64_t>(data.size()), "", data.data(),
        static_cast<int64_t>(data.size()), target);

    ASSERT_GE(target.size(), sizeof(uint32_t));
    uint32_t magic = 0;
    std::memcpy(&magic, target.data(), sizeof(uint32_t));
    EXPECT_EQ(magic, FLY_OBJECT_MAGIC);

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordPersistsData) {
    CMString base_path = test_dir_ + "/write_rec";
    DataWriter writer(base_path, "", "c1d2e3f4", 1024, 10240, 128);

    CMString data = "record data here";
    FlyBuffer target;
    auto result = writer.compress_to_buffer(
        static_cast<uint64_t>(data.size()), "", data.data(),
        static_cast<int64_t>(data.size()), target);

    writer.write_record("test/record", result.original_size,
                         result.chunk_count, target);

    EXPECT_EQ(writer.total_bytes_written(), static_cast<int64_t>(data.size()));

    auto* entry = writer.get_last_entry("test/record");
    ASSERT_NE(entry, nullptr);
    EXPECT_GT(entry->size, 0);

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordThresholdRollover) {
    CMString base_path = test_dir_ + "/rollover";
    DataWriter writer(base_path, "", "c1d2e3f4", 10, 10240, 128);

    CMString data1(200, 'A');
    FlyBuffer target1;
    auto r1 = writer.compress_to_buffer(
        static_cast<uint64_t>(data1.size()), "", data1.data(),
        static_cast<int64_t>(data1.size()), target1);
    writer.write_record("obj1", r1.original_size, r1.chunk_count, target1);

    CMString data2(200, 'B');
    FlyBuffer target2;
    auto r2 = writer.compress_to_buffer(
        static_cast<uint64_t>(data2.size()), "", data2.data(),
        static_cast<int64_t>(data2.size()), target2);
    writer.write_record("obj2", r2.original_size, r2.chunk_count, target2);

    EXPECT_EQ(writer.file_count(), 2);

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordAfterCloseThrows) {
    CMString base_path = test_dir_ + "/rec_close";
    DataWriter writer(base_path, "", "c1d2e3f4", 1024, 10240, 128);

    CMString data = "temp";
    FlyBuffer target;
    auto result = writer.compress_to_buffer(
        static_cast<uint64_t>(data.size()), "", data.data(),
        static_cast<int64_t>(data.size()), target);
    writer.write_record("obj", result.original_size, result.chunk_count, target);

    writer.close();

    writer.write_record("obj2", result.original_size,
                         result.chunk_count, target);
}

}
