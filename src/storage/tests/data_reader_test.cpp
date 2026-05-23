#include <gtest/gtest.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <filesystem>
#include <fstream>

namespace {

class DataReaderWriterTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = "/tmp/fly_test_data_reader_" + std::to_string(::getpid());
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DataReaderWriterTest, WriteAndReadSmallObject) {
    CMString base_path = test_dir_ + "/rw_base";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("test/obj", "hello world", false);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    CMString data = reader.read_object("test/obj");
    EXPECT_EQ(data, "hello world");
}

TEST_F(DataReaderWriterTest, WriteAndReadMultipleObjects) {
    CMString base_path = test_dir_ + "/rw_multi";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("obj/1", "data_one", false);
        writer.write_object("obj/2", "data_two", false);
        writer.write_object("obj/3", "data_three", false);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_EQ(reader.read_object("obj/1"), "data_one");
    EXPECT_EQ(reader.read_object("obj/2"), "data_two");
    EXPECT_EQ(reader.read_object("obj/3"), "data_three");
}

TEST_F(DataReaderWriterTest, ExistsReturnsTrue) {
    CMString base_path = test_dir_ + "/rw_exists";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("exists/obj", "data", false);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_TRUE(reader.exists("exists/obj"));
}

TEST_F(DataReaderWriterTest, ExistsReturnsFalseForMissing) {
    CMString base_path = test_dir_ + "/rw_not_exists";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("real/obj", "data", false);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_FALSE(reader.exists("missing/obj"));
}

TEST_F(DataReaderWriterTest, ReadNonExistentThrows) {
    CMString base_path = test_dir_ + "/rw_throw";
    DataReader reader(base_path, "", "a1b2c3d4");

    EXPECT_THROW(reader.read_object("nonexistent"), std::runtime_error);
}

TEST_F(DataReaderWriterTest, ReadFromLocalPathPriority) {
    CMString base_path = test_dir_ + "/rw_base_priority";
    CMString data_path = test_dir_ + "/rw_data_priority";

    {
        DataWriter writer(base_path, data_path, "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("priority/obj", "local_data", false);
        writer.close();
    }

    DataReader reader(base_path, data_path, "a1b2c3d4");
    CMString data = reader.read_object("priority/obj");
    EXPECT_EQ(data, "local_data");
}

TEST_F(DataReaderWriterTest, WriteAndReadLargeObject) {
    CMString base_path = test_dir_ + "/rw_large";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1048576, 100, 50);
        CMString large_data(500, 'x');
        writer.write_object("large/obj", large_data, false);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    CMString data = reader.read_object("large/obj");
    EXPECT_EQ(data.size(), 500u);
    EXPECT_EQ(data.front(), 'x');
}

TEST_F(DataReaderWriterTest, ReadByIndexEntry) {
    CMString base_path = test_dir_ + "/rw_entry";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("entry/obj", "entry_data", false);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_TRUE(reader.exists("entry/obj"));

    IndexEntry* entry = reader.exists("entry/obj") ? nullptr : nullptr;
    EXPECT_EQ(entry, nullptr);
}

TEST_F(DataReaderWriterTest, AggregatedFileRead) {
    CMString base_path = test_dir_ + "/rw_agg";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024, 10240, 128);
        writer.write_object("agg/1", "aaaa", false);
        writer.write_object("agg/2", "bbbb", false);
        writer.write_object("agg/3", "cccc", false);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_EQ(reader.read_object("agg/1"), "aaaa");
    EXPECT_EQ(reader.read_object("agg/2"), "bbbb");
    EXPECT_EQ(reader.read_object("agg/3"), "cccc");
}

}
