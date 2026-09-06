#include <gtest/gtest.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/local_index.h>
#include <storage/cpp/compressing_streambuf.h>
#include <storage/cpp/fly_buffer_stream.h>
#include <common/buffer/cpp/fly_buffer.h>
#include <common/testing/cpp/test_helpers.h>
#include <common/serialization/cpp/object_header.h>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace {

class DataWriterTest : public ::testing::Test {
protected:
    CMString test_dir_;

    void SetUp() override {
        test_dir_ = fly::test::qa_tmp_dir("fly_test_data_writer");
        std::filesystem::create_directories(test_dir_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    struct TestRecord {
        int64_t original_size_;
        int32_t chunk_count_;
        FlyBuffer buffer;
    };

    TestRecord make_record(const CMString& data, const CMString& py_name = "") {
        TestRecord rec;
        ObjectHeader header;
        header.total_size_ = 0;
        header.chunk_count_ = 0;
        header.compression_type_ = 0;
        header.py_name_ = py_name;
        header.py_name_len_ = static_cast<uint16_t>(py_name.size());
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
            rec.original_size_ = csbuf.total_uncompressed();
            rec.chunk_count_ = csbuf.chunk_count();
        }
        counting_stream.flush();

        header.total_size_ = static_cast<uint64_t>(rec.original_size_);
        header.chunk_count_ = static_cast<uint32_t>(rec.chunk_count_);
        CMString real_header = header.serialize();
        std::memcpy(rec.buffer.data(), real_header.data(), real_header.size());

        return rec;
    }
};

TEST_F(DataWriterTest, WriteRecordPersistsData) {
    CMString db_path = test_dir_ + "/write_rec";
    DataWriter writer(db_path, "", "c1d2e3f4", 1024);

    CMString data = "record data here";
    auto rec = make_record(data);
    writer.write_record("test/record", rec.original_size_, rec.chunk_count_, rec.buffer);

    EXPECT_EQ(writer.total_bytes_written(), static_cast<int64_t>(data.size()));

    auto entry = writer.get_last_entry("test/record");
    ASSERT_TRUE(entry.has_value());
    EXPECT_GT(entry->size_, 0);

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordThresholdRollover) {
    CMString db_path = test_dir_ + "/rollover";
    DataWriter writer(db_path, "", "c1d2e3f4", 10);

    CMString data1(200, 'A');
    auto r1 = make_record(data1);
    writer.write_record("obj1", r1.original_size_, r1.chunk_count_, r1.buffer);

    CMString data2(200, 'B');
    auto r2 = make_record(data2);
    writer.write_record("obj2", r2.original_size_, r2.chunk_count_, r2.buffer);

    EXPECT_EQ(writer.file_count(), 2);

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordAfterCloseLogs) {
    CMString db_path = test_dir_ + "/rec_close";
    DataWriter writer(db_path, "", "c1d2e3f4", 1024);

    CMString data = "temp";
    auto rec = make_record(data);
    writer.write_record("obj", rec.original_size_, rec.chunk_count_, rec.buffer);
    writer.close();

    writer.write_record("obj2", rec.original_size_, rec.chunk_count_, rec.buffer);
}

TEST_F(DataWriterTest, MultipleRecords) {
    CMString db_path = test_dir_ + "/multi";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    CMString data1 = "hello world";
    auto r1 = make_record(data1);
    writer.write_record("obj1", r1.original_size_, r1.chunk_count_, r1.buffer);

    CMString data2 = "another record";
    auto r2 = make_record(data2);
    writer.write_record("obj2", r2.original_size_, r2.chunk_count_, r2.buffer);

    EXPECT_EQ(writer.total_bytes_written(), static_cast<int64_t>(data1.size() + data2.size()));

    auto e1 = writer.get_last_entry("obj1");
    ASSERT_TRUE(e1.has_value());
    EXPECT_GT(e1->size_, 0);

    auto e2 = writer.get_last_entry("obj2");
    ASSERT_TRUE(e2.has_value());
    EXPECT_GT(e2->size_, 0);

    writer.close();
}

TEST_F(DataWriterTest, RemoveEntry) {
    CMString db_path = test_dir_ + "/remove";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    CMString data = "to be removed";
    auto rec = make_record(data);
    writer.write_record("target", rec.original_size_, rec.chunk_count_, rec.buffer);

    auto entry = writer.get_last_entry("target");
    ASSERT_TRUE(entry.has_value());

    EXPECT_TRUE(writer.remove_entry("target"));
    EXPECT_EQ(writer.get_last_entry("target"), std::nullopt);

    writer.close();
}

TEST_F(DataWriterTest, WriteWithCustomDataPath) {
    CMString db_path = test_dir_ + "/base_custom";
    CMString data_path = test_dir_ + "/data_custom";
    DataWriter writer(db_path, data_path, "a1b2c3d4", 1024);

    CMString data = "hello";
    auto rec = make_record(data);
    writer.write_record("custom/obj", rec.original_size_, rec.chunk_count_, rec.buffer);

    std::filesystem::path dp(data_path);
    EXPECT_TRUE(std::filesystem::exists(dp));

    writer.close();
}

TEST_F(DataWriterTest, FileCountIncrements) {
    CMString db_path = test_dir_ + "/count_base";
    DataWriter writer(db_path, "", "a1b2c3d4", 20);

    EXPECT_EQ(writer.file_count(), 1);

    CMString data1(15, 'a');
    auto r1 = make_record(data1);
    writer.write_record("obj1", r1.original_size_, r1.chunk_count_, r1.buffer);
    EXPECT_EQ(writer.file_count(), 1);

    CMString data2(15, 'b');
    auto r2 = make_record(data2);
    writer.write_record("obj2", r2.original_size_, r2.chunk_count_, r2.buffer);
    EXPECT_EQ(writer.file_count(), 2);

    writer.close();
}

TEST_F(DataWriterTest, DoubleCloseIsSafe) {
    CMString db_path = test_dir_ + "/double_close";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    CMString data = "close test";
    auto rec = make_record(data);
    writer.write_record("obj", rec.original_size_, rec.chunk_count_, rec.buffer);
    writer.close();
    EXPECT_NO_THROW(writer.close());
}

TEST_F(DataWriterTest, RemoveEntryReturnsFalseForMissing) {
    CMString db_path = test_dir_ + "/remove_miss";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    EXPECT_FALSE(writer.remove_entry("nonexistent_obj"));

    writer.close();
}

TEST_F(DataWriterTest, GetAllEntriesReturnsNulloptForMissing) {
    CMString db_path = test_dir_ + "/get_all_miss";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    auto entries = writer.get_all_entries("no_such_obj");
    EXPECT_FALSE(entries.has_value());

    writer.close();
}

TEST_F(DataWriterTest, WriteRecordWithWriteContextHash) {
    CMString db_path = test_dir_ + "/ctx_hash";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    CMString data = "ctx hash data";
    auto rec = make_record(data);
    writer.write_record("ctx/obj", rec.original_size_, rec.chunk_count_, rec.buffer, "hash123");

    auto entry = writer.get_last_entry("ctx/obj");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->write_context_hash_, "hash123");

    writer.close();
}

TEST_F(DataWriterTest, TotalBytesAccumulatesAcrossRecords) {
    CMString db_path = test_dir_ + "/total_bytes";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    CMString d1 = "first";
    auto r1 = make_record(d1);
    writer.write_record("a", r1.original_size_, r1.chunk_count_, r1.buffer);
    EXPECT_EQ(writer.total_bytes_written(), static_cast<int64_t>(d1.size()));

    CMString d2 = "second_record";
    auto r2 = make_record(d2);
    writer.write_record("b", r2.original_size_, r2.chunk_count_, r2.buffer);
    EXPECT_EQ(writer.total_bytes_written(), static_cast<int64_t>(d1.size() + d2.size()));

    writer.close();
}

TEST_F(DataWriterTest, GetAllEntriesForSingleObject) {
    CMString db_path = test_dir_ + "/get_all";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024);

    CMString data = "single";
    auto rec = make_record(data);
    writer.write_record("single/obj", rec.original_size_, rec.chunk_count_, rec.buffer);

    auto entries = writer.get_all_entries("single/obj");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 1u);

    writer.close();
}

TEST_F(DataWriterTest, FlushAfterWritePersistsIndex) {
    CMString db_path = test_dir_ + "/flush_idx";
    CMString writer_id = "f1ush2id";

    {
        DataWriter writer(db_path, "", writer_id, 1024);
        CMString data = "flush test";
        auto rec = make_record(data);
        writer.write_record("flush/obj", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.flush();

        EXPECT_TRUE(std::filesystem::exists(db_path + "/" + writer_id + ".idx"));
        writer.close();
    }
}

TEST_F(DataWriterTest, HostStoredInEntry) {
    CMString db_path = test_dir_ + "/host_test";
    DataWriter writer(db_path, "", "a1b2c3d4", 1024, "192.168.1.1");

    CMString data = "host data";
    auto rec = make_record(data);
    writer.write_record("host/obj", rec.original_size_, rec.chunk_count_, rec.buffer);

    auto entry = writer.get_last_entry("host/obj");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->host_, "192.168.1.1");

    writer.close();
}

// =============================================================================
// abort_segment 测试 —— data 文件 truncate 回滚
// =============================================================================

// 辅助：获取 data 文件大小
static int64_t file_size(const CMString& path) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(path, ec);
    return ec ? -1 : static_cast<int64_t>(sz);
}

TEST_F(DataWriterTest, AbortSegmentTruncatesDataFile) {
    // BEGIN → 写 obj1 → abort_segment：data 文件 truncate 回 BEGIN 偏移(0)
    CMString db_path = test_dir_ + "/abort_truncate";
    CMString data_file;

    {
        DataWriter writer(db_path, "", "abort1", 100000);
        data_file = db_path + "/data_abort1_001.dat";

        writer.mark_begin();
        CMString data(500, 'X');
        auto rec = make_record(data);
        writer.write_record("obj/dirty", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.flush();

        // 此时 data 文件有内容
        EXPECT_GT(file_size(data_file), 0);
        int64_t dirty_size = file_size(data_file);

        writer.abort_segment();
        writer.flush();

        // abort 后 data 文件应 truncate 回 BEGIN 偏移(0)
        EXPECT_EQ(file_size(data_file), 0);
        EXPECT_LT(file_size(data_file), dirty_size);

        writer.close();
    }
}

TEST_F(DataWriterTest, AbortNoOpWhenSegmentNotOpened) {
    // 未 mark_begin 直接 abort_segment：no-op，不应崩溃
    CMString db_path = test_dir_ + "/abort_noop";
    DataWriter writer(db_path, "", "abort2", 100000);

    EXPECT_FALSE(writer.segment_active());
    writer.abort_segment();   // no-op
    EXPECT_FALSE(writer.segment_active());

    writer.close();
}

TEST_F(DataWriterTest, AbortAcrossRollover) {
    // BEGIN → 写超过 threshold 触发 rollover（产生 _002.dat）→ abort
    // 应回滚：删除 _002.dat + truncate _001.dat 回 BEGIN 偏移
    CMString db_path = test_dir_ + "/abort_rollover";
    CMString file1 = db_path + "/data_abort3_001.dat";
    CMString file2 = db_path + "/data_abort3_002.dat";

    {
        DataWriter writer(db_path, "", "abort3", 200);   // 小 threshold 触发 rollover

        // 先写一个段外记录占位 _001.dat（让 BEGIN 偏移 > 0）
        CMString data0(100, 'P');
        auto rec0 = make_record(data0);
        writer.write_record("obj/keep", rec0.original_size_, rec0.chunk_count_, rec0.buffer);
        writer.flush();
        int64_t begin_offset = file_size(file1);
        EXPECT_GT(begin_offset, 0);

        writer.mark_begin();   // 记录回滚点 = 当前 _001.dat 偏移

        // 写大记录触发 rollover 到 _002.dat
        CMString data1(300, 'A');
        auto rec1 = make_record(data1);
        writer.write_record("obj/dirty1", rec1.original_size_, rec1.chunk_count_, rec1.buffer);
        writer.flush();

        CMString data2(300, 'B');
        auto rec2 = make_record(data2);
        writer.write_record("obj/dirty2", rec2.original_size_, rec2.chunk_count_, rec2.buffer);
        writer.flush();

        EXPECT_TRUE(std::filesystem::exists(file2));   // rollover 产生了 _002.dat

        writer.abort_segment();
        writer.flush();

        // _002.dat 应被删除，_001.dat 应 truncate 回 BEGIN 偏移
        EXPECT_FALSE(std::filesystem::exists(file2));
        EXPECT_EQ(file_size(file1), begin_offset);

        writer.close();
    }

    // 验证段外记录 obj/keep 仍可读（idx 里保留）
    // abort 后再 load idx，只有段外的 keep
    LocalIndex idx(db_path + "/abort3.idx");
    idx.load();
    EXPECT_TRUE(idx.find_entry("obj/keep").has_value());
    EXPECT_FALSE(idx.find_entry("obj/dirty1").has_value());
    EXPECT_FALSE(idx.find_entry("obj/dirty2").has_value());
}

TEST_F(DataWriterTest, AbortLargeObjectInEmptyFile) {
    // BEGIN 时文件为空，写一个超过 threshold 的大对象（不触发 rollover，直接写入空文件）
    // abort 后应 truncate 回 0
    CMString db_path = test_dir_ + "/abort_large_empty";
    CMString data_file = db_path + "/data_abort5_001.dat";

    {
        DataWriter writer(db_path, "", "abort5", 200);   // threshold=200

        writer.mark_begin();   // 此时 current_file_size_ == 0

        // 大对象：500 字节 >> threshold 200。但因 current_file_size_==0 不 rollover
        CMString data(500, 'L');
        auto rec = make_record(data);
        writer.write_record("obj/large_dirty", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.flush();

        EXPECT_GT(file_size(data_file), 400);   // 大对象已写入

        writer.abort_segment();
        writer.flush();

        // truncate 回 BEGIN 偏移 0
        EXPECT_EQ(file_size(data_file), 0);

        writer.close();
    }

    LocalIndex idx(db_path + "/abort5.idx");
    idx.load();
    EXPECT_FALSE(idx.find_entry("obj/large_dirty").has_value());
}

TEST_F(DataWriterTest, AbortLargeObjectTriggersRollover) {
    // BEGIN 时文件非空，写大对象触发 rollover 到新文件
    // abort 后新文件应被删除，原文件 truncate 回 BEGIN 偏移
    CMString db_path = test_dir_ + "/abort_large_rollover";
    CMString file1 = db_path + "/data_abort6_001.dat";
    CMString file2 = db_path + "/data_abort6_002.dat";

    {
        DataWriter writer(db_path, "", "abort6", 200);

        // 先写段外小对象占位 _001.dat
        CMString data0(100, 'P');
        auto rec0 = make_record(data0);
        writer.write_record("obj/keep", rec0.original_size_, rec0.chunk_count_, rec0.buffer);
        writer.flush();
        int64_t begin_offset = file_size(file1);
        EXPECT_GT(begin_offset, 0);

        writer.mark_begin();   // 回滚点 = _001.dat 偏移 begin_offset

        // 大对象：500 字节 >> threshold。begin_offset + 500 > 200 且 begin_offset > 0 → rollover
        CMString big(500, 'L');
        auto rec = make_record(big);
        writer.write_record("obj/large_dirty", rec.original_size_, rec.chunk_count_, rec.buffer);
        writer.flush();

        EXPECT_TRUE(std::filesystem::exists(file2));   // rollover 产生了 _002.dat
        EXPECT_GT(file_size(file2), 400);              // 大对象在 _002.dat

        writer.abort_segment();
        writer.flush();

        // _002.dat 删除，_001.dat truncate 回 begin_offset
        EXPECT_FALSE(std::filesystem::exists(file2));
        EXPECT_EQ(file_size(file1), begin_offset);

        writer.close();
    }

    LocalIndex idx(db_path + "/abort6.idx");
    idx.load();
    EXPECT_TRUE(idx.find_entry("obj/keep").has_value());
    EXPECT_FALSE(idx.find_entry("obj/large_dirty").has_value());
}

TEST_F(DataWriterTest, AbortMultipleObjectsAcrossFiles) {
    // 场景：a/b/c 写入文件 A，d/e/f 触发阈值 rollover 到文件 B。
    // abort 后 B 应删除，A truncate 回 BEGIN 偏移，a-f 全部从 idx 丢弃。
    //
    // 用极小 threshold（1）强制每次写入都 rollover（首个写空文件除外）。
    // mark_begin 前写一个段外 keep 占位 _001，BEGIN 后 a 写 _001（触发 rollover
    // 条件 current>0），b→_002, c→_003... 但我们要测的是"a/b/c 在同一文件"，
    // 所以用大 threshold 让 a/b/c 聚合在一个文件，d 触发 rollover。
    //
    // 直接构造 FlyBuffer 写入原始字节（绕过压缩），精确控制大小：
    CMString db_path = test_dir_ + "/abort_multi_files";
    CMString file1 = db_path + "/data_abort7_001.dat";

    auto make_raw = [](int size, char fill) -> FlyBuffer {
        FlyBuffer buf;
        buf.write(CMString(size, fill).data(), size);
        return buf;
    };

    {
        // threshold=300: a/b/c 各 80 字节累计 240 < 300（在 _001），d 80 字节
        // 240+80=320 > 300 且 240>0 → rollover 到 _002，e/f 在 _002
        DataWriter writer(db_path, "", "abort7", 300);

        writer.mark_begin();   // 回滚点 = _001.dat 偏移 0

        for (char c : {'a', 'b', 'c'}) {
            auto buf = make_raw(80, c);
            writer.write_record(CMString("obj/") + c, 80, 1, buf);
        }
        writer.flush();
        EXPECT_FALSE(std::filesystem::exists(db_path + "/data_abort7_002.dat"))
            << "a/b/c should fit in _001";

        for (char c : {'d', 'e', 'f'}) {
            auto buf = make_raw(80, c);
            writer.write_record(CMString("obj/") + c, 80, 1, buf);
        }
        writer.flush();
        CMString file2 = db_path + "/data_abort7_002.dat";
        EXPECT_TRUE(std::filesystem::exists(file2)) << "d should trigger rollover to _002";
        EXPECT_GT(file_size(file2), 0);

        writer.abort_segment();
        writer.flush();

        // _002.dat 删除，_001.dat truncate 回 BEGIN 偏移 0
        EXPECT_FALSE(std::filesystem::exists(file2));
        EXPECT_EQ(file_size(file1), 0);

        writer.close();
    }

    // idx: a-f 全部丢弃（ABORT 段）
    LocalIndex idx(db_path + "/abort7.idx");
    idx.load();
    for (char c : {'a', 'b', 'c', 'd', 'e', 'f'}) {
        EXPECT_FALSE(idx.find_entry(CMString("obj/") + c).has_value())
            << "obj/" << c << " should be discarded";
    }
}

TEST_F(DataWriterTest, BeginEndThenAbortNextSegment) {
    // 第一段 BEGIN→ADD→END（提交），第二段 BEGIN→ADD→abort（撤销）
    // 验证第一段数据保留，第二段回滚
    CMString db_path = test_dir_ + "/two_segments";
    CMString data_file = db_path + "/data_abort4_001.dat";

    {
        DataWriter writer(db_path, "", "abort4", 100000);

        // 第一段：提交
        writer.mark_begin();
        CMString d1(200, '1');
        auto r1 = make_record(d1);
        writer.write_record("obj/committed", r1.original_size_, r1.chunk_count_, r1.buffer);
        writer.flush();
        writer.mark_end();

        int64_t committed_size = file_size(data_file);
        EXPECT_GT(committed_size, 0);

        // 第二段：撤销
        writer.mark_begin();
        CMString d2(300, '2');
        auto r2 = make_record(d2);
        writer.write_record("obj/rolledback", r2.original_size_, r2.chunk_count_, r2.buffer);
        writer.flush();
        EXPECT_GT(file_size(data_file), committed_size);   // 第二段写入了更多字节

        writer.abort_segment();
        writer.flush();

        // data 文件应回滚到第一段 END 后的大小
        EXPECT_EQ(file_size(data_file), committed_size);

        writer.close();
    }

    LocalIndex idx(db_path + "/abort4.idx");
    idx.load();
    EXPECT_TRUE(idx.find_entry("obj/committed").has_value());
    EXPECT_FALSE(idx.find_entry("obj/rolledback").has_value());
}


// ════════════════════════════════════════════════════════════════════
// L1 增量写守卫：未 begin 的 append/finish 是 no-op；closed 后 begin 拒绝。
// ════════════════════════════════════════════════════════════════════

TEST_F(DataWriterTest, IncrementalGuardsWithoutBegin) {
    CMString db_path = test_dir_ + "/inc_guard";
    DataWriter writer(db_path, "", "incg", 1 << 20);

    // 未 begin：append 是 no-op（不崩、不写）。
    writer.append_incremental("ORPHAN", 6);
    // 未 begin：finish 是 no-op（无 entry）。
    writer.finish_incremental("obj/ghost", 6, 1, "");

    writer.flush();
    EXPECT_FALSE(writer.get_all_entries("obj/ghost").has_value())
        << "未 begin 的增量写不得产生 entry";
    EXPECT_EQ(writer.total_bytes_written(), 0);

    // 正常 begin → append → finish 后 entry 出现（守卫不误伤正常路径）。
    int64_t off = writer.begin_incremental();
    ASSERT_GE(off, 0);
    writer.append_incremental("HELLO", 5);
    writer.finish_incremental("obj/real", 5, 1, "");
    auto entry = writer.get_last_entry("obj/real");
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->size_, 5);
}

TEST_F(DataWriterTest, ClosedWriterRejectsIncrementalBegin) {
    CMString db_path = test_dir_ + "/inc_closed";
    {
        DataWriter writer(db_path, "", "incc", 1 << 20);
        writer.close();
        EXPECT_EQ(writer.begin_incremental(), -1)
            << "closed 后 begin_incremental 必须拒绝";
    }
}
}
