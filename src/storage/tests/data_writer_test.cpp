#include <gtest/gtest.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/compressing_streambuf.h>
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

    struct TestRecord {
        int64_t original_size;
        int32_t chunk_count;
        FlyBuffer buffer;
    };

    TestRecord make_record(const CMString& data, const CMString& py_name = "") {
        TestRecord rec;
        ObjectHeader header;
        header.total_size = 0;
        header.chunk_count = 0;
        header.compression_type = 0;
        header.py_name = py_name;
        header.py_name_len = static_cast<uint16_t>(py_name.size());
        CMString header_bytes = header.serialize();

        FlyBufferStreamBuf fly_buf(rec.buffer);
        CountingStreamBuf counting_buf(fly_buf);
        std::ostream counting_stream(&counting_buf);
        counting_stream.write(header_bytes.data(), static_cast<std::streamsize>(header_bytes.size()));

        {
            CompressingStreamBuf csbuf(counting_stream, nullptr, 4096);
            std::ostream os(&csbuf);
            os.write(data.data(), static_cast<std::streamsize>(data.size()));
            os.flush();
            rec.original_size = csbuf.total_uncompressed();
            rec.chunk_count = csbuf.chunk_count();
        }
        counting_stream.flush();

        header.total_size = static_cast<uint64_t>(rec.original_size);
        header.chunk_count = static_cast<uint32_t>(rec.chunk_count);
        CMString real_header = header.serialize();
        std::memcpy(rec.buffer.data(), real_header.data(), real_header.size());

        return rec;
    }
};

TEST_F(DataWriterTest, WriteRecordPersistsData) {
    CMString base_path = test_dir_ + "/write_rec";
    DataWriter writer(base_path, "", "c1d2e3f4", 1024);

    CMString data = "record data here";
    auto rec = make_record(data);
    writer.write_record("test/record", rec.original_size, rec.chunk_count, rec.buffer);

    EXPECT_EQ(writer.total_bytes_written(), static_cast<int64_t>(data.size()));

    auto entry = writer.get_last_entry("test/record");
    ASSERT_TRUE(entry.has_value());
    EXPECT_GT(entry->size, 0);

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordThresholdRollover) {
    CMString base_path = test_dir_ + "/rollover";
    DataWriter writer(base_path, "", "c1d2e3f4", 10);

    CMString data1(200, 'A');
    auto r1 = make_record(data1);
    writer.write_record("obj1", r1.original_size, r1.chunk_count, r1.buffer);

    CMString data2(200, 'B');
    auto r2 = make_record(data2);
    writer.write_record("obj2", r2.original_size, r2.chunk_count, r2.buffer);

    EXPECT_EQ(writer.file_count(), 2);

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordAfterCloseLogs) {
    CMString base_path = test_dir_ + "/rec_close";
    DataWriter writer(base_path, "", "c1d2e3f4", 1024);

    CMString data = "temp";
    auto rec = make_record(data);
    writer.write_record("obj", rec.original_size, rec.chunk_count, rec.buffer);
    writer.close();

    writer.write_record("obj2", rec.original_size, rec.chunk_count, rec.buffer);
}

TEST_F(DataWriterTest, MultipleRecords) {
    CMString base_path = test_dir_ + "/multi";
    DataWriter writer(base_path, "", "a1b2c3d4", 1024);

    CMString data1 = "hello world";
    auto r1 = make_record(data1);
    writer.write_record("obj1", r1.original_size, r1.chunk_count, r1.buffer);

    CMString data2 = "another record";
    auto r2 = make_record(data2);
    writer.write_record("obj2", r2.original_size, r2.chunk_count, r2.buffer);

    EXPECT_EQ(writer.total_bytes_written(), static_cast<int64_t>(data1.size() + data2.size()));

    auto e1 = writer.get_last_entry("obj1");
    ASSERT_TRUE(e1.has_value());
    EXPECT_GT(e1->size, 0);

    auto e2 = writer.get_last_entry("obj2");
    ASSERT_TRUE(e2.has_value());
    EXPECT_GT(e2->size, 0);

    writer.close();
}

TEST_F(DataWriterTest, RemoveEntry) {
    CMString base_path = test_dir_ + "/remove";
    DataWriter writer(base_path, "", "a1b2c3d4", 1024);

    CMString data = "to be removed";
    auto rec = make_record(data);
    writer.write_record("target", rec.original_size, rec.chunk_count, rec.buffer);

    auto entry = writer.get_last_entry("target");
    ASSERT_TRUE(entry.has_value());

    EXPECT_TRUE(writer.remove_entry("target"));
    EXPECT_EQ(writer.get_last_entry("target"), std::nullopt);

    writer.close();
}

TEST_F(DataWriterTest, WriteWithCustomDataPath) {
    CMString base_path = test_dir_ + "/base_custom";
    CMString data_path = test_dir_ + "/data_custom";
    DataWriter writer(base_path, data_path, "a1b2c3d4", 1024);

    CMString data = "hello";
    auto rec = make_record(data);
    writer.write_record("custom/obj", rec.original_size, rec.chunk_count, rec.buffer);

    std::filesystem::path dp(data_path);
    EXPECT_TRUE(std::filesystem::exists(dp));

    writer.close();
}

TEST_F(DataWriterTest, FileCountIncrements) {
    CMString base_path = test_dir_ + "/count_base";
    DataWriter writer(base_path, "", "a1b2c3d4", 20);

    EXPECT_EQ(writer.file_count(), 1);

    CMString data1(15, 'a');
    auto r1 = make_record(data1);
    writer.write_record("obj1", r1.original_size, r1.chunk_count, r1.buffer);
    EXPECT_EQ(writer.file_count(), 1);

    CMString data2(15, 'b');
    auto r2 = make_record(data2);
    writer.write_record("obj2", r2.original_size, r2.chunk_count, r2.buffer);
    EXPECT_EQ(writer.file_count(), 2);

    writer.close();
}

}
