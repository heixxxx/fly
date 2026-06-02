#include <gtest/gtest.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <serialization/cpp/object_header.h>
#include <filesystem>

namespace {

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

CMString decompress_raw(const CMString& raw) {
    DecompressingStreamBuf dsbuf(raw.data(), raw.size());
    std::istream is(&dsbuf);
    CMString result;
    CMVector<char> tmp(4096);
    while (is) {
        is.read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        if (is.gcount() > 0) {
            result.append(tmp.data(), static_cast<size_t>(is.gcount()));
        }
    }
    return result;
}

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

TEST_F(DataReaderWriterTest, WriteAndReadRawBytes) {
    CMString base_path = test_dir_ + "/rw_base";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("hello world");
        writer.write_record("test/obj", rec.original_size, rec.chunk_count, rec.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    CMString raw = reader.read_raw_bytes("test/obj");
    ASSERT_FALSE(raw.empty());

    CMString data = decompress_raw(raw);
    EXPECT_EQ(data, "hello world");
}

TEST_F(DataReaderWriterTest, WriteAndReadMultipleObjects) {
    CMString base_path = test_dir_ + "/rw_multi";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024);
        auto r1 = make_record("data_one");
        writer.write_record("obj/1", r1.original_size, r1.chunk_count, r1.buffer);
        auto r2 = make_record("data_two");
        writer.write_record("obj/2", r2.original_size, r2.chunk_count, r2.buffer);
        auto r3 = make_record("data_three");
        writer.write_record("obj/3", r3.original_size, r3.chunk_count, r3.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_EQ(decompress_raw(reader.read_raw_bytes("obj/1")), "data_one");
    EXPECT_EQ(decompress_raw(reader.read_raw_bytes("obj/2")), "data_two");
    EXPECT_EQ(decompress_raw(reader.read_raw_bytes("obj/3")), "data_three");
}

TEST_F(DataReaderWriterTest, ExistsReturnsTrue) {
    CMString base_path = test_dir_ + "/rw_exists";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("data");
        writer.write_record("exists/obj", rec.original_size, rec.chunk_count, rec.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_TRUE(reader.exists("exists/obj"));
}

TEST_F(DataReaderWriterTest, ExistsReturnsFalseForMissing) {
    CMString base_path = test_dir_ + "/rw_not_exists";

    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_FALSE(reader.exists("missing/obj"));
}

TEST_F(DataReaderWriterTest, ReadNonExistentReturnsEmpty) {
    CMString base_path = test_dir_ + "/rw_empty";
    DataReader reader(base_path, "", "a1b2c3d4");
    EXPECT_TRUE(reader.read_raw_bytes("nonexistent").empty());
}

TEST_F(DataReaderWriterTest, PyNameRoundtrip) {
    CMString base_path = test_dir_ + "/rw_pyname";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("test data", "MyClass");
        writer.write_record("named/obj", rec.original_size, rec.chunk_count, rec.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    CMString raw = reader.read_raw_bytes("named/obj");
    ASSERT_FALSE(raw.empty());

    DecompressingStreamBuf dsbuf(raw.data(), raw.size());
    EXPECT_EQ(dsbuf.py_name(), "MyClass");
}

TEST_F(DataReaderWriterTest, FindEntryReturnsEntryForExisting) {
    CMString base_path = test_dir_ + "/rw_find";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("find_data");
        writer.write_record("find/obj", rec.original_size, rec.chunk_count, rec.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("find/obj");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->object_name, "find/obj");
    EXPECT_GT(entry->size, 0);
}

TEST_F(DataReaderWriterTest, FindEntryReturnsNulloptForMissing) {
    CMString base_path = test_dir_ + "/rw_find_miss";
    DataReader reader(base_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("missing/obj");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(DataReaderWriterTest, FindAllEntriesReturnsEntries) {
    CMString base_path = test_dir_ + "/rw_find_all";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 10);
        auto r1 = make_record("data1");
        writer.write_record("multi/obj", r1.original_size, r1.chunk_count, r1.buffer);
        auto r2 = make_record("data2");
        writer.write_record("multi/obj", r2.original_size, r2.chunk_count, r2.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    auto entries = reader.find_all_entries("multi/obj");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 2u);
}

TEST_F(DataReaderWriterTest, FindAllEntriesReturnsNulloptForMissing) {
    CMString base_path = test_dir_ + "/rw_find_all_miss";
    DataReader reader(base_path, "", "a1b2c3d4");
    auto entries = reader.find_all_entries("no/such/obj");
    EXPECT_FALSE(entries.has_value());
}

TEST_F(DataReaderWriterTest, FindFilePathWithEmptyDataPath) {
    CMString base_path = test_dir_ + "/rw_filepath";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("filepath data");
        writer.write_record("fp/obj", rec.original_size, rec.chunk_count, rec.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("fp/obj");
    ASSERT_TRUE(entry.has_value());

    CMString found_path = reader.find_file_path(entry->file_name);
    EXPECT_FALSE(found_path.empty());
    EXPECT_TRUE(std::filesystem::exists(found_path));
}

TEST_F(DataReaderWriterTest, ReadFromMissingFileReturnsEmpty) {
    CMString base_path = test_dir_ + "/rw_missing_file";
    DataReader reader(base_path, "", "a1b2c3d4");

    IndexEntry fake_entry;
    fake_entry.object_name = "fake";
    fake_entry.file_name = "nonexistent_file.dat";
    fake_entry.offset = 0;
    fake_entry.size = 10;

    CMString result = reader.read_raw_bytes(fake_entry);
    EXPECT_TRUE(result.empty());
}

TEST_F(DataReaderWriterTest, ReadRawBytesByIndexEntry) {
    CMString base_path = test_dir_ + "/rw_by_entry";

    {
        DataWriter writer(base_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("entry data");
        writer.write_record("entry/obj", rec.original_size, rec.chunk_count, rec.buffer);
        writer.close();
    }

    DataReader reader(base_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("entry/obj");
    ASSERT_TRUE(entry.has_value());

    CMString raw = reader.read_raw_bytes(entry.value());
    ASSERT_FALSE(raw.empty());
    CMString data = decompress_raw(raw);
    EXPECT_EQ(data, "entry data");
}

TEST_F(DataReaderWriterTest, DataPathFallback) {
    CMString base_path = test_dir_ + "/rw_base_fb";
    CMString data_path = test_dir_ + "/rw_data_fb";

    {
        DataWriter writer(base_path, data_path, "a1b2c3d4", 1024);
        auto rec = make_record("fallback data");
        writer.write_record("fb/obj", rec.original_size, rec.chunk_count, rec.buffer);
        writer.close();
    }

    DataReader reader(base_path, data_path, "a1b2c3d4");
    CMString raw = reader.read_raw_bytes("fb/obj");
    ASSERT_FALSE(raw.empty());
    CMString data = decompress_raw(raw);
    EXPECT_EQ(data, "fallback data");
}

}
