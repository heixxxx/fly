#include <gtest/gtest.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/data_reader.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/test_helpers.h>
#include <filesystem>

namespace {

struct TestRecord {
    int64_t original_size_;
    int32_t chunk_count_;
    FlyBuffer buffer;
};

TestRecord make_record(const CMString& data, const CMString& py_name = "") {
    TestRecord rec;
    ObjectHeader header;
    header.compression_type_ = 0;
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    // 新格式（§4.4）：块流纯追加，完成后追加 trailer。
    FlyBufferStreamBuf fly_buf(rec.buffer);
    CountingStreamBuf counting_buf(fly_buf);
    std::ostream counting_stream(&counting_buf);

    {
        CompressingStreamBuf csbuf(counting_stream, nullptr, 4096);
        std::ostream os(&csbuf);
        os.write(data.data(), static_cast<std::streamsize>(data.size()));
        os.flush();
        rec.original_size_ = csbuf.total_uncompressed();
        rec.chunk_count_ = csbuf.chunk_count();
        header.compression_type_ = static_cast<uint8_t>(csbuf.effective_compression_type());
        header.block_comp_lens_ = csbuf.block_comp_lens();  // B' 块表
    }
    counting_stream.flush();

    header.total_size_ = static_cast<uint64_t>(rec.original_size_);
    header.chunk_count_ = static_cast<uint32_t>(rec.chunk_count_);
    CMString trailer = header.serialize_trailer();
    rec.buffer.write(trailer.data(), trailer.size());

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

// Convenience: extract CMString from a FlyBufferPtr (for decompress_raw calls).
CMString raw_str(const FlyBufferPtr& buf) {
    if (!buf) return {};
    return CMString(buf->data(), buf->size());
}

class DataReaderWriterTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = fly::test::qa_tmp_dir("fly_test_data_reader");
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }
};

TEST_F(DataReaderWriterTest, WriteAndReadRawBytes) {
    CMString db_path = test_dir_ + "/rw_base";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("hello world");
        writer.write_record("test/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    FlyBufferPtr raw = reader.read_raw_bytes("test/obj");
    ASSERT_FALSE(!raw || raw->empty());

    CMString data = decompress_raw(raw_str(raw));
    EXPECT_EQ(data, "hello world");
}

TEST_F(DataReaderWriterTest, WriteAndReadMultipleObjects) {
    CMString db_path = test_dir_ + "/rw_multi";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 1024);
        auto r1 = make_record("data_one");
        writer.write_record("obj/1", r1.original_size_, r1.chunk_count_, r1.buffer);
        auto r2 = make_record("data_two");
        writer.write_record("obj/2", r2.original_size_, r2.chunk_count_, r2.buffer);
        auto r3 = make_record("data_three");
        writer.write_record("obj/3", r3.original_size_, r3.chunk_count_, r3.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    EXPECT_EQ(decompress_raw(raw_str(reader.read_raw_bytes("obj/1"))), "data_one");
    EXPECT_EQ(decompress_raw(raw_str(reader.read_raw_bytes("obj/2"))), "data_two");
    EXPECT_EQ(decompress_raw(raw_str(reader.read_raw_bytes("obj/3"))), "data_three");
}

TEST_F(DataReaderWriterTest, ExistsReturnsTrue) {
    CMString db_path = test_dir_ + "/rw_exists";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("data");
        writer.write_record("exists/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    EXPECT_TRUE(reader.exists("exists/obj"));
}

TEST_F(DataReaderWriterTest, ExistsReturnsFalseForMissing) {
    CMString db_path = test_dir_ + "/rw_not_exists";

    DataReader reader(db_path, "", "a1b2c3d4");
    EXPECT_FALSE(reader.exists("missing/obj"));
}

TEST_F(DataReaderWriterTest, ReadNonExistentReturnsEmpty) {
    CMString db_path = test_dir_ + "/rw_empty";
    DataReader reader(db_path, "", "a1b2c3d4");
    EXPECT_TRUE(!reader.read_raw_bytes("nonexistent"));
}

TEST_F(DataReaderWriterTest, PyNameRoundtrip) {
    CMString db_path = test_dir_ + "/rw_pyname";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("test data", "MyClass");
        writer.write_record("named/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    FlyBufferPtr raw = reader.read_raw_bytes("named/obj");
    ASSERT_FALSE(!raw || raw->empty());

    DecompressingStreamBuf dsbuf(raw->data(), raw->size());
    EXPECT_EQ(dsbuf.py_name(), "MyClass");
}

TEST_F(DataReaderWriterTest, FindEntryReturnsEntryForExisting) {
    CMString db_path = test_dir_ + "/rw_find";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("find_data");
        writer.write_record("find/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("find/obj");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->object_name_, "find/obj");
    EXPECT_GT(entry->size_, 0);
}

TEST_F(DataReaderWriterTest, FindEntryReturnsNulloptForMissing) {
    CMString db_path = test_dir_ + "/rw_find_miss";
    DataReader reader(db_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("missing/obj");
    EXPECT_FALSE(entry.has_value());
}

TEST_F(DataReaderWriterTest, FindAllEntriesReturnsEntries) {
    CMString db_path = test_dir_ + "/rw_find_all";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 10);
        auto r1 = make_record("data1");
        writer.write_record("multi/obj", r1.original_size_, r1.chunk_count_, r1.buffer);
        auto r2 = make_record("data2");
        writer.write_record("multi/obj", r2.original_size_, r2.chunk_count_, r2.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    auto entries = reader.find_all_entries("multi/obj");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 2u);
}

TEST_F(DataReaderWriterTest, FindAllEntriesReturnsNulloptForMissing) {
    CMString db_path = test_dir_ + "/rw_find_all_miss";
    DataReader reader(db_path, "", "a1b2c3d4");
    auto entries = reader.find_all_entries("no/such/obj");
    EXPECT_FALSE(entries.has_value());
}

TEST_F(DataReaderWriterTest, FindFilePathWithEmptyDataPath) {
    CMString db_path = test_dir_ + "/rw_filepath";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("filepath data");
        writer.write_record("fp/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("fp/obj");
    ASSERT_TRUE(entry.has_value());

    CMString found_path = reader.find_file_path(entry->file_name_);
    EXPECT_FALSE(found_path.empty());
    EXPECT_TRUE(std::filesystem::exists(found_path));
}

TEST_F(DataReaderWriterTest, ReadFromMissingFileReturnsEmpty) {
    CMString db_path = test_dir_ + "/rw_missing_file";
    DataReader reader(db_path, "", "a1b2c3d4");

    IndexEntry fake_entry;
    fake_entry.object_name_ = "fake";
    fake_entry.file_name_ = "nonexistent_file.dat";
    fake_entry.offset_ = 0;
    fake_entry.size_ = 10;

    FlyBufferPtr result = reader.read_raw_bytes(fake_entry);
    EXPECT_TRUE(!result || result->empty());
}

TEST_F(DataReaderWriterTest, ReadRawBytesByIndexEntry) {
    CMString db_path = test_dir_ + "/rw_by_entry";

    {
        DataWriter writer(db_path, "", "a1b2c3d4", 1024);
        auto rec = make_record("entry data");
        writer.write_record("entry/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.close();
    }

    DataReader reader(db_path, "", "a1b2c3d4");
    auto entry = reader.find_entry("entry/obj");
    ASSERT_TRUE(entry.has_value());

    FlyBufferPtr raw = reader.read_raw_bytes(entry.value());
    ASSERT_FALSE(!raw || raw->empty());
    CMString data = decompress_raw(raw_str(raw));
    EXPECT_EQ(data, "entry data");
}

TEST_F(DataReaderWriterTest, DataPathFallback) {
    CMString db_path = test_dir_ + "/rw_base_fb";
    CMString data_path = test_dir_ + "/rw_data_fb";

    {
        DataWriter writer(db_path, data_path, "a1b2c3d4", 1024);
        auto rec = make_record("fallback data");
        writer.write_record("fb/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.close();
    }

    DataReader reader(db_path, data_path, "a1b2c3d4");
    FlyBufferPtr raw = reader.read_raw_bytes("fb/obj");
    ASSERT_FALSE(!raw || raw->empty());
    CMString data = decompress_raw(raw_str(raw));
    EXPECT_EQ(data, "fallback data");
}

}
