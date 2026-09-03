// 零容忍数据校验语义测试（chunked-transfer-design.md §5 / 测试 10-12）。
//
// 语义锚定：
//   校验类错误（帧头 check / wire 根 CRC / 磁盘块 CRC / trailer）→
//   [FATAL-DATA-CORRUPTION] ERR → 失效缓存 → 【一次】对象级重取（远程换副本
//   优先 / 仅本地则绕过缓存重读盘）→ 仍败（任何方式：仍校验失败/断连/超时/
//   无数据）→ DataCorruptionError 上抛（Python 面转 RuntimeError FATAL →
//   TaskFailed；worker 不崩溃）。
//   不做静默重试循环——持续校验失败 = 内存/硬件/代码缺陷，必须大声暴露。
#include <gtest/gtest.h>
#include <storage/cpp/data_service.h>
#include <storage/cpp/database.h>
#include <storage/cpp/data_writer.h>
#include <storage/cpp/decompressing_streambuf.h>
#include <storage/cpp/disk_chunk_source.h>
#include <storage/cpp/memory_chunk_source.h>
#include <serialization/cpp/object_header.h>
#include <common/cpp/fly_buffer.h>
#include <common/cpp/data_checksum.h>
#include <common/cpp/error_types.h>
#include <common/cpp/test_helpers.h>
#include <log/cpp/logger.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fly {
namespace {

// 组装合法新格式 record（单 raw 块 + trailer）。
FlyBufferPtr make_valid_record(const std::string& data, const CMString& py_name) {
    auto record = CMMakeShared<FlyBuffer>();
    int32_t sz = static_cast<int32_t>(data.size());
    uint64_t crc = data_checksum(data.data(), data.size());
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&sz), 4);
    record->write(reinterpret_cast<const char*>(&crc), 8);
    record->write(data.data(), data.size());
    ObjectHeader header;
    header.total_size_ = data.size();
    header.chunk_count_ = 1;
    header.py_name_ = py_name;
    header.py_name_len_ = static_cast<uint16_t>(py_name.size());
    header.compression_type_ = 0;
    header.block_comp_lens_ = {static_cast<uint32_t>(data.size())};  // B' 块表
    CMString trailer = header.serialize_trailer();
    record->write(trailer.data(), trailer.size());
    return record;
}

}  // namespace

class DataCorruptionTest : public ::testing::Test {
protected:
    CMString test_dir_;
    CMSharedPtr<DataService> ds_ = DataService::instance();

    void SetUp() override {
        test_dir_ = fly::test::qa_tmp_dir("fly_test_corrupt");
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        ds_->reset();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    // 写一个对象并完成登记（盘上有完整合法 record）。
    void write_valid_object(const CMString& db_path, const CMString& full,
                            const std::string& data) {
        ds_->register_database(db_path, test_dir_ + "/data");
        ds_->on_write_started(db_path, full);
        DataWriter writer(test_dir_, test_dir_ + "/data", "dcw", 0);
        auto rec = make_valid_record(data, "bytes");
        writer.write_record(full, data.size(), 1, *rec, "");
        writer.flush();
        auto entries = writer.get_all_entries(full);
        ASSERT_TRUE(entries.has_value());
        ds_->on_write_completed(db_path, full, entries.value());
        ds_->on_object_flushed(full);
    }
};

// 测试 10（LocalRetryRecoversFromBadCache）已删除（T4 2026-08-31）：其前提
// "读可命中 low-tier 缓存条目"随 §4.7 读恒走数据源 + low_ 池删除不复存在；
// 盘上/远程损坏路径由下方 LocalDiskCorruptFatal / RemoteChunkCorruptRetryThenFatal 覆盖。

// 测试 11：盘上损坏且无远程副本 → 重读仍败 → DataCorruptionError（FATAL）。
TEST_F(DataCorruptionTest, LocalDiskCorruptFatal) {
    CMString db_path = "/dcbad";
    CMString full = db_path + ":obj";
    std::string good = "object that will be corrupted on disk";
    write_valid_object(db_path, full, good);

    // 位腐 trailer：读出 .dat 字节翻转末字节（trailer CRC 域）后原长度写回。
    // （保持文件大小不变——对象区间读取不越界，损坏精确落在 trailer 校验上。）
    std::filesystem::path data_dir(test_dir_ + "/data");
    for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
        if (entry.path().filename().string().rfind("data_dcw", 0) == 0) {
            std::ifstream in(entry.path(), std::ios::binary);
            CMString bytes((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
            in.close();
            ASSERT_GT(bytes.size(), 8u);
            bytes[bytes.size() - 1] = static_cast<char>(bytes[bytes.size() - 1] ^ 0x01);
            std::ofstream out(entry.path(), std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), bytes.size());
        }
    }

    Database db(db_path, test_dir_ + "/data");
    // 盘坏 → 重取（无远程副本 → 必败）→ FATAL 异常。
    EXPECT_THROW({
        try {
            db.read_object_compressed("obj", false);
        } catch (const DataCorruptionError& e) {
            EXPECT_NE(std::string(e.what()).find("[FATAL-DATA-CORRUPTION]"), std::string::npos);
            throw;
        }
    }, DataCorruptionError);
}

// 测试 12：远程校验失败的一次重取编排（cb 注入，确定性）。
TEST_F(DataCorruptionTest, RemoteChunkCorruptRetryThenFatal) {
    // 场景 C：CHECKSUM → 干净成功 = 成功（一次重取预算内换副本命中）。
    {
        CMString full = "/dc12a:obj";
        ds_->update_remote_idx(full, 1, "host_a", 8000);
        ds_->update_remote_idx(full, 2, "host_b", 9000);

        auto good = make_valid_record("clean data", "bytes");
        ds_->set_direct_compressed_read_handler(
            [&](const CMString& host, int32_t, const CMString&)
                -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
                if (host == "host_a") {
                    return {false, nullptr, {}, {}, ReadError::CHECKSUM};
                }
                return {true, good, "bytes", {}, ReadError::NONE};
            });

        auto [found, raw, py, hash, csp] = ds_->read_raw_compressed(full);
        EXPECT_TRUE(found);
        EXPECT_TRUE(raw && !raw->empty());
        ds_->set_direct_compressed_read_handler(nullptr);
    }

    // 场景 A：CHECKSUM → CHECKSUM = fatal（预算耗尽）。
    {
        CMString full = "/dc12b:obj";
        ds_->update_remote_idx(full, 1, "host_a", 8000);
        ds_->update_remote_idx(full, 2, "host_b", 9000);

        ds_->set_direct_compressed_read_handler(
            [](const CMString&, int32_t, const CMString&)
                -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
                return {false, nullptr, {}, {}, ReadError::CHECKSUM};
            });

        EXPECT_THROW(ds_->read_raw_compressed(full), DataCorruptionError);
        ds_->set_direct_compressed_read_handler(nullptr);
    }

    // 场景 B：CHECKSUM → 断连 = fatal（重取模式下任何失败都不可接受，§5）。
    {
        CMString full = "/dc12c:obj";
        ds_->update_remote_idx(full, 1, "host_a", 8000);
        ds_->update_remote_idx(full, 2, "host_b", 9000);

        ds_->set_direct_compressed_read_handler(
            [&](const CMString& host, int32_t, const CMString&)
                -> std::tuple<bool, FlyBufferPtr, CMString, CMString, ReadError> {
                if (host == "host_a") {
                    return {false, nullptr, {}, {}, ReadError::CHECKSUM};
                }
                return {false, nullptr, {}, {}, ReadError::NETWORK};
            });

        EXPECT_THROW(ds_->read_raw_compressed(full), DataCorruptionError);
        ds_->set_direct_compressed_read_handler(nullptr);
    }
}

// ── 失败分类（chunk_source.h failure_detail 契约，2026-09-04）──
// IO 失败（open/pread errno）与完整性失败（截断/CRC/trailer）分流：
// 消费端据此不再把 IO 读失败误报为 [FATAL-DATA-CORRUPTION]。

TEST_F(DataCorruptionTest, DiskSourceOpenFailureClassifiedAsIo) {
    CMString missing = test_dir_ + "/no_such_file.dat";
    DiskChunkSource src(missing, 0, 16, "n", 16, 1, 0);
    EXPECT_TRUE(src.failed());
    EXPECT_TRUE(src.failure_detail().rfind("io:", 0) == 0)
        << "detail=" << src.failure_detail();
}

TEST_F(DataCorruptionTest, DiskSourceShortReadClassifiedAsIntegrity) {
    CMString path = test_dir_ + "/short.dat";
    {
        std::ofstream f(path, std::ios::binary);
        f.write("0123456789", 10);
    }
    // 声明区间 64B > 实际 10B → pull 短读 = record 截断（完整性，非 IO）。
    DiskChunkSource src(path, 0, 64, "n", 64, 1, 0);
    char buf[64];
    EXPECT_EQ(src.pull(buf, sizeof(buf)), -1);
    EXPECT_TRUE(src.failed());
    EXPECT_TRUE(src.failure_detail().rfind("integrity:", 0) == 0)
        << "detail=" << src.failure_detail();
}

TEST_F(DataCorruptionTest, StreamBufFailureDetailPassesThrough) {
    CMString missing = test_dir_ + "/gone.dat";
    auto src = CMMakeShared<DiskChunkSource>(missing, 0, 16, "n", 16, 1, 0);
    ASSERT_TRUE(src->failed());
    DecompressingStreamBuf sb(src, 16);
    char tmp[8];
    sb.sgetn(tmp, sizeof(tmp));
    EXPECT_TRUE(sb.checksum_failed());
    EXPECT_TRUE(sb.failure_detail().rfind("io:", 0) == 0)
        << "detail=" << sb.failure_detail();
}

TEST_F(DataCorruptionTest, MemorySourceTrailerFailureClassifiedAsIntegrity) {
    CMString bad = "this is not a valid record at all";
    MemoryChunkSource src(bad.data(), bad.size());
    EXPECT_TRUE(src.failed());
    EXPECT_TRUE(src.failure_detail().rfind("integrity:", 0) == 0)
        << "detail=" << src.failure_detail();
    // 成功路径 detail 为空。
    auto good = make_valid_record("ok", "bytes");
    MemoryChunkSource ok_src(good->data(), good->size());
    EXPECT_FALSE(ok_src.failed());
    EXPECT_TRUE(ok_src.failure_detail().empty());
}

}  // namespace fly
